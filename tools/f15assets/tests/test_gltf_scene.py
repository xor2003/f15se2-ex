from __future__ import annotations

import copy
import json
import math
import struct
import tempfile
import unittest
from pathlib import Path

from f15assets.validation_model3d import glb_to_glmesh_bytes


class GltfSceneTest(unittest.TestCase):
    def setUp(self):
        self.doc = {
            "asset": {"version": "2.0"}, "buffers": [{"byteLength": 36}],
            "bufferViews": [{"buffer": 0, "byteLength": 36}],
            "accessors": [{"bufferView": 0, "componentType": 5126,
                           "count": 3, "type": "VEC3"}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "mode": 4}]}],
            "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
        }

    def convert(self):
        payload = struct.pack("<9f", 1, 0, 0, 0, 1, 0, 0, 0, 0)
        encoded = json.dumps(self.doc).encode()
        encoded += b" " * (-len(encoded) % 4)
        data = struct.pack("<III", 0x46546c67, 2, 28 + len(encoded) + len(payload))
        data += struct.pack("<II", len(encoded), 0x4e4f534a) + encoded
        data += struct.pack("<II", len(payload), 0x004e4942) + payload
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "scene.glb"
            path.write_bytes(data)
            mesh = glb_to_glmesh_bytes(path)
        count = struct.unpack_from("<I", mesh, 40)[0]
        cursor = 44
        primitives = []
        for _ in range(count):
            mode, vertices = struct.unpack_from("<II", mesh, cursor)
            self.assertEqual(mode, 4)
            cursor += 40
            primitives.append([struct.unpack_from("<3f", mesh, cursor + i*12)
                               for i in range(vertices)])
            cursor += vertices * 12
        self.assertEqual(cursor, len(mesh))
        return primitives

    def test_identity(self):
        self.assertEqual(self.convert(), [[(1, 0, 0), (0, 1, 0), (0, 0, 0)]])

    def test_translation(self):
        self.doc["nodes"][0]["translation"] = [10, 0, 0]
        self.assertEqual(self.convert()[0], [(11, 0, 0), (10, 1, 0), (10, 0, 0)])

    def test_parent_transform_and_trs_order(self):
        self.doc["nodes"] = [
            {"children": [1], "translation": [10, 0, 0]},
            {"mesh": 0, "translation": [1, 2, 3], "scale": [2, 3, 4],
             "rotation": [0, 0, math.sqrt(0.5), math.sqrt(0.5)]},
        ]
        points = self.convert()[0]
        for actual, expected in zip(points, [(11, 4, 3), (8, 2, 3), (11, 2, 3)]):
            for value, reference in zip(actual, expected):
                self.assertAlmostEqual(value, reference, places=5)

    def test_matrix_and_mesh_instances(self):
        self.doc["nodes"] = [
            {"mesh": 0},
            {"mesh": 0, "matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 5,6,7,1]},
        ]
        self.doc["scenes"][0]["nodes"] = [0, 1]
        instances = self.convert()
        self.assertEqual(len(instances), 2)
        self.assertEqual(instances[0][0], (1, 0, 0))
        self.assertEqual(instances[1][0], (6, 6, 7))

    def test_selected_scene_omits_unused_meshes(self):
        self.doc["meshes"].append(copy.deepcopy(self.doc["meshes"][0]))
        self.doc["nodes"].append({"mesh": 1, "translation": [5, 0, 0]})
        self.doc["scenes"].append({"nodes": [1]})
        self.doc["scene"] = 1
        instances = self.convert()
        self.assertEqual(len(instances), 1)
        self.assertEqual(instances[0][0], (6, 0, 0))

    def test_scene_less_hierarchy(self):
        del self.doc["scenes"]
        del self.doc["scene"]
        self.doc["nodes"] = [{"children": [1]}, {"mesh": 0}]
        self.assertEqual(len(self.convert()), 1)

    def test_invalid_scene_graph(self):
        for roots, nodes in [([-1], [{"mesh": 0}]), ([2], [{"mesh": 0}]),
                             ([0], [{"mesh": -1}]),
                             ([0], [{"children": [0]}]),
                             ([0, 0], [{"mesh": 0}]),
                             ([0], [{"children": [1, 1]}, {"mesh": 0}])]:
            with self.subTest(roots=roots, nodes=nodes):
                self.doc["nodes"] = nodes
                self.doc["scenes"] = [{"nodes": roots}]
                with self.assertRaises(ValueError):
                    self.convert()

    def test_invalid_or_unrepresentable_transform(self):
        for transform in [{"translation": [1, 2]}, {"rotation": [0, 0, 0, 0]},
                          {"translation": [float("inf"), 0, 0]},
                          {"scale": [1e40, 1, 1]},
                          {"matrix": [1]*16},
                          {"matrix": [1]*16, "scale": [1, 1, 1]}]:
            with self.subTest(transform=transform):
                self.doc["nodes"] = [{"mesh": 0, **transform}]
                with self.assertRaises(ValueError):
                    self.convert()

    def test_unsupported_deformation_is_not_silently_ignored(self):
        for key, value in [("skin", 0), ("weights", [1])]:
            with self.subTest(key=key):
                self.doc["nodes"] = [{"mesh": 0, key: value}]
                with self.assertRaises(ValueError):
                    self.convert()
        self.doc["nodes"] = [{"mesh": 0}]
        self.doc["animations"] = [{}]
        with self.assertRaises(ValueError):
            self.convert()
