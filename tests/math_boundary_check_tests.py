"""Regression checks for the transitional math-boundary allowlist."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "boundaries", Path(__file__).resolve().parents[1] / "tools/check_math_boundaries.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class BoundaryChecks(unittest.TestCase):
    def test_access_is_rejected_outside_allowlist(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            path = root / "src/gameplay.cpp"
            for text in ('#include "math/boundary.hpp"',
                         '#include "math/legacy_rotation.hpp"',
                         '#include "fixed_math.hpp"',
                         '#define F15_MATH_BOUNDARY_ACCESS',
                         'auto angle = f15::fixed::Angle16::raw(1);',
                         'using C = f15::math::Boundary<FixedBackend>;',
                         'using namespace f15::math::legacy;'):
                with self.subTest(text=text):
                    path.write_text(text)
                    self.assertEqual(len(checker.violations(root)), 1)
            path.write_text('#include "math/rotation.hpp"')
            self.assertEqual(checker.violations(root), [])


if __name__ == "__main__":
    unittest.main()
