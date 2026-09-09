import copy
import unittest

from f15assets.gltf_primitive import validate_primitive


class GltfPrimitiveTest(unittest.TestCase):
    def setUp(self):
        self.doc = {"accessors": [{"type": "VEC3", "componentType": 5126, "count": 3}],
                    "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1, 0, 0, 1]}}]}
        self.primitive = {"attributes": {"POSITION": 0}, "material": 0}

    def test_flat_material_triangle(self):
        validate_primitive(self.doc, self.primitive)

    def test_unsupported_geometry(self):
        for fields in ({"mode": 5}, {"mode": True}, {"attributes": {}},
                       {"attributes": {"POSITION": -1}},
                       {"attributes": {"POSITION": 0, "COLOR_0": 1}},
                       {"indices": 0}, {"mode": 1}, {"material": -1}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                validate_primitive(self.doc, {**self.primitive, **fields})

    def test_unsupported_material(self):
        for pbr in ({"baseColorTexture": {"index": 0}},
                    {"baseColorFactor": [float("nan"), 0, 0, 1]},
                    {"baseColorFactor": [2, 0, 0, 1]},
                    {"baseColorFactor": [1, 0, 0]}, None):
            with self.subTest(pbr=pbr), self.assertRaises(ValueError):
                doc = copy.deepcopy(self.doc)
                doc["materials"][0]["pbrMetallicRoughness"] = pbr
                validate_primitive(doc, self.primitive)

    def test_indexed_line(self):
        self.doc["accessors"].append({"type": "SCALAR", "componentType": 5123, "count": 2})
        self.primitive.update(mode=1, indices=1)
        validate_primitive(self.doc, self.primitive)
