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
                         '#include "math/control_boundary.hpp"',
                         '#include "math/legacy_flight_control.hpp"',
                         'using C = f15::math::ControlBoundary<FixedBackend>;',
                         'using C = f15::math::AltitudeBoundary<FixedBackend>;',
                         '#include "math/altitude_boundary.hpp"',
                         '#include "math/legacy_altitude.hpp"',
                         '#include "math/legacy_horizontal.hpp"',
                         '#include "math/horizontal_boundary.hpp"',
                         'using C = f15::math::HorizontalBoundary<FixedBackend>;',
                         '#include "math/airspeed_boundary.hpp"',
                         '#include "math/legacy_airspeed.hpp"',
                         'using C = f15::math::AirspeedBoundary<FixedBackend>;',
                         '#include "math/propulsion_boundary.hpp"',
                         '#include "math/legacy_propulsion.hpp"',
                         '#include "math/legacy_map.hpp"',
                         'using C = f15::math::MapBoundary<FixedBackend>;',
                         'using C = f15::math::PropulsionBoundary<FixedBackend>;',
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

    def test_raw_scalar_globals_need_allowlist_entries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/state.h").write_text('extern int16 g_newQuantity;\n'
                                            'extern int16 g_reviewed, g_other;\n')
            (root / "src/state.c").write_text('int16 g_newQuantity = 0;\n'
                                             'int16 g_reviewed = 1, g_other;\n'
                                             'static int16 hiddenLocal;\n'
                                             'extern int16 g_proto(void);\n')
            # No allowlist file: every declared scalar is unreviewed.
            errors = checker.declaration_violations(root)
            self.assertEqual(sum("g_newQuantity" in e for e in errors), 1)
            self.assertEqual(sum("g_reviewed" in e for e in errors), 1)
            self.assertFalse(any("hiddenLocal" in e for e in errors))
            self.assertFalse(any("g_proto" in e for e in errors))
            # Reviewed entries pass; names no longer declared are stale.
            (root / "tools").mkdir()
            (root / "tools/sim_global_allowlist.txt").write_text(
                "g_newQuantity\ng_reviewed\ng_other\ng_retired\n")
            errors = checker.declaration_violations(root)
            self.assertEqual(errors,
                             ["tools/sim_global_allowlist.txt: stale entry "
                              "'g_retired' (no longer a raw scalar)"])


if __name__ == "__main__":
    unittest.main()
