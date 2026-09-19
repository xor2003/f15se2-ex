"""Enforce the reviewed raw-conversion boundary for the migrated math scope."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
# Transitional adapters are debt, not an exemption for future domain code.
ALLOWED = {
    "src/math/rotation.hpp", "src/math/boundary.hpp", "src/math/legacy_rotation.hpp",
    "src/eg3drast.c", "src/eg3dcam.c",
}
ACCESS = re.compile(r"F15_MATH_BOUNDARY_ACCESS|\bBoundary\s*<|math::legacy|\bfixed\s*::|"
                    r'#\s*include\s*[<"](?:math/)?(?:boundary|legacy_rotation)\.hpp[>"]|'
                    r'#\s*include\s*[<"]fixed_math\.hpp[>"]')

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
