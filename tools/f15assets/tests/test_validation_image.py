from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from PIL import Image

from f15assets.validation_image import (
    _read_indexed_png_pixels_and_palette,
    validate_pic_png_replacement,
    validate_png_replacement_loadability,
)


class ImageValidationTest(unittest.TestCase):
    def test_truncated_pixels_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "picture.png"
            Image.new("RGB", (16, 16), "red").save(path)
            self.assertEqual(validate_png_replacement_loadability(path), [])
            data = path.read_bytes()
            path.write_bytes(data[:data.index(b"IDAT") + 6])
            self.assertTrue(validate_png_replacement_loadability(path))

    def test_loadability_does_not_read_original(self):
        def forbidden_read(path):
            self.fail("loadability-only must not decode the original file")

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "picture.png"
            Image.new("RGB", (32, 32), "blue").save(path)
            self.assertEqual(validate_pic_png_replacement(
                Path("missing.PIC"), path, forbidden_read, loadability_only=True), [])

    def test_indexed_pixel_and_palette_comparison_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "picture.png"
            image = Image.new("P", (2, 1))
            palette = [0, 0, 0, 255, 0, 0] + [0] * 762
            image.putpalette(palette)
            image.putdata([0, 1])
            image.save(path)
            self.assertEqual(_read_indexed_png_pixels_and_palette(path, 2, 1),
                             (b"\x00\x01", bytes(palette)))
            with self.assertRaises(ValueError):
                _read_indexed_png_pixels_and_palette(path, 1, 1)
