"""Enforce the reviewed raw-conversion boundary for the migrated math scope."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
# Transitional adapters are debt, not an exemption for future domain code.
ALLOWED = {
    "src/math/rotation.hpp", "src/math/boundary.hpp", "src/math/legacy_rotation.hpp",
    "src/math/interpolation.hpp",
    "src/math/flight_control.hpp", "src/math/control_boundary.hpp",
    "src/math/legacy_flight_control.hpp",
    "src/math/altitude.hpp", "src/math/altitude_boundary.hpp", "src/math/legacy_altitude.hpp",
    "src/math/horizontal.hpp", "src/math/horizontal_boundary.hpp", "src/math/legacy_horizontal.hpp",
    "src/math/airspeed.hpp", "src/math/airspeed_boundary.hpp", "src/math/legacy_airspeed.hpp",
    "src/math/aerodynamics.hpp",
    "src/math/guidance.hpp",
    "src/math/map_position.hpp", "src/math/legacy_map.hpp",
    "src/math/propulsion.hpp", "src/math/propulsion_boundary.hpp", "src/math/legacy_propulsion.hpp",
    "src/replacement_terrain_collision.h",
    # Global storage seeds typed quantities from legacy unit constants.
    "src/egdata.c",
    # SDL samples and persisted device calibration enter the typed input domain.
    "src/joystick.c",
    # Recorded diagnostics serialize quantities in the legacy recording units.
    "src/shared/blackbox_diag.c", "src/shared/blackbox_snapshot.c",
    "src/eg3drast.c", "src/eg3dcam.c", "src/egflight.c",
    # Snapshot interpolation writes the legacy render word through renderWord.
    "src/egsys.c",
    # Temporary Euler consumers; remove each entry as its scalar math is migrated.
    "src/egtarget.c", "src/egkeys.c", "src/egmath.c", "src/eghudr.c", "src/eghudm.c",
    "src/egtgt2.c", "src/egcombat.c", "src/egframe.c", "src/egtacmap.c", "src/egthreat.c",
    "src/egui.c",
    # The world load/save seam seeds the SimObject fine shadow from packed fields.
    "src/worldxfer.c",
}
ACCESS = re.compile(r"F15_MATH_BOUNDARY_ACCESS|\b(?:Control|Altitude|Horizontal|Airspeed|Propulsion|Map)?Boundary\s*<|math::legacy|\bfixed\s*::|"
                    r'#\s*include\s*[<"](?:math/)?(?:boundary|control_boundary|altitude_boundary|horizontal_boundary|airspeed_boundary|propulsion_boundary|legacy_rotation|legacy_flight_control|legacy_altitude|legacy_horizontal|legacy_airspeed|legacy_propulsion)\.hpp[>"]|'
                    r'#\s*include\s*[<"](?:fixed_math|(?:math/)?legacy_map)\.hpp[>"]')

def violations(root):
    errors = []
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        relative = path.relative_to(root).as_posix()
        if relative in ALLOWED:
            continue
        for line, text in enumerate(path.read_text().splitlines(), 1):
            if ACCESS.search(text):
                errors.append(f"{relative}:{line}: unreviewed raw math access")
    return errors

if __name__ == "__main__":
    errors = violations(ROOT)
    print("\n".join(errors) if errors else "Math boundary allowlist passed")
    sys.exit(bool(errors))
