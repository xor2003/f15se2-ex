# Fixed and floating-point math migration

Status: 2026-09-19. Typed fixed and double-precision rotation backends are
implemented in `src/math/rotation.hpp`. Production matrix builders, matrix
multiplication and camera rotation now use the typed fixed implementation.
This is the first migrated subsystem, not a complete interchangeable simulation
backend. No whole-game floating-point backend selector is exposed.

## Rotation migration checkpoint

* `Angle`, `Coefficient`, `EulerAngles` and `Matrix3` carry backend types. Storage
  is private; primitive construction/extraction and mixed-backend operations
  are not public APIs. Eight negative compilation tests enforce representative
  misuse cases, with a positive compilation control.
* Fixed builders preserve intermediate rounding, word wrapping, LUT interpolation
  and matrix-product truncation. The production adapters also preserve the six
  horizon scratch globals and existing return values.
* Modern builders use radians and double-precision matrices, retaining fractions
  across matrix products. A 100,000-step test retains increments smaller than one
  legacy angle word. Orthogonality, handedness and inverse conventions are tested.
* The historical object builder is not just the transpose of the positive-angle
  camera builder. Modern `objectRotation(a)` is `transpose(rotation(-a))`.
* `boundary.hpp` provides explicitly gated representation adapters;
  `legacy_rotation.hpp` is temporary compatibility debt for the raw global state.
  The source allowlist is enforced by `tools/check_math_boundaries.py`, with its
  own rejection tests. Python is required for native test configuration. This
  textual check is not an AST audit of all numerical state and cannot prove
  whole-game migration or detect every spelling/alias of a bypass.
* The fixed oracle in `tests/math_rotation_reference.hpp` preserves the pre-routing
  algorithms from `9018a8b` using defined widened arithmetic. It does not call the
  new library. Tests exhaust 65,536 sine inputs, compare 10,000 Euler triples,
  arbitrary word-matrix products, camera and flight rotation/rebuild callers,
  and 2,000 production internal object-builder cases. The LUT is still shared
  with production; this is current-port compatibility evidence, not DOS binary proof.

Verification at this checkpoint: Linux Release build and all 44 CTests pass.
Clang analysis of the typed test translation unit reports no diagnostics.
ASan/UBSan passes for that instrumented translation unit and its inline math;
the linked production core was not sanitizer-instrumented. Windows, Android,
browser, live-flight and external-asset validation were not run in this checkpoint.

### Next acceptance boundary

Persistent orientation state, attitude recovery and refresh policy must move
together before selecting modern flight math. `applyRotationDelta` and
`rebuildOrientation` are regression-covered callers, but their matrices and Euler
globals remain raw words. Do not enable a floating option that writes each result
back into those globals. Characterize the inverse-angle endpoint behavior first,
then migrate flight position/velocity/forces and their consumers with explicit
units, frames and file/render adapters. Projection, terrain, combat and AI remain
unmigrated. The final acceptance requirements below still apply.

## Provenance and import corrections

The classes came from `f15se2-re/main`, commit `6cbbec1`, originally introduced
by `746567d`. That repository was only read. We imported the header and its
standalone tests, not its production changes or CMake symbol-renaming scheme.

Review found these problems in the original library and harness:

* `WordPair32::join()` left-shifted negative signed values, undefined in C++17.
  The import uses a widened multiplication/addition with a representable result.
* Signed division negated INT32_MIN before converting to unsigned. The import
  forms magnitudes and restores signs with unsigned arithmetic.
* Arithmetic-shift helpers also negated INT32_MIN. The import uses widened
  arithmetic; shift counts must be in [0, 31]. The standalone test's reference
  helper had the same defect and now uses quotient/remainder instead.
* `computeBearing()` did not preserve the current engine's signed-word absolute
  value and intermediate ratio stores. For example, (-32768, 1) returned
  0xc000 instead of the engine's 0. The imported version explicitly models these
  stores and uses multiplication instead of shifting a negative numerator.
* The old test CMake applied symbol-renaming definitions as SOURCE properties,
  affecting production targets too. Its tests passed while its `egame` target
  failed to link. This import uses this repository's existing LINK_CORE model,
  without renaming production functions or compiling a modified reference copy.

Those import corrections were confined to the new library/tests. The subsequent
rotation routing preserves fixed outputs; it does not change resource layouts,
input widths or introduce new flight-model formulas.

## Coverage and gaps

| Area | Library coverage | Work remaining before backend replacement |
| --- | --- | --- |
| Angles/trigonometry | Typed fixed/double rotation angles and trig; imported inverse trig | Inverse-trig endpoint contracts and remaining scalar trig callers |
| Fixed products | Q15, rounded result, high-word products and carry variants | Audit every caller's rounding and destination narrowing; Q15 inputs are signed words |
| Long arithmetic | WordPair32, shifts, full/saturated division | Document valid domains, divide-by-zero behavior and overflow policy per operation |
| Orientation | Typed fixed/double builders and product; fixed production routing | Extract stateful attitude recovery and orientation refresh from egflight.c |
| Projection/HUD | Selected projection, clipping, HUD rotation helpers | Verify against current rasterizer and HUD state, including invalid/depth sentinels |
| Range/bearing | Legacy approximation and bearing helpers | Keep gameplay distance approximation distinct from Euclidean distance |
| Camera precision | Not complete | sinMulQ8/cosMulQ8, camera fractional remainders, fine-coordinate bearing/range in egmath.c |
| Terrain/world coordinates | Not complete | scaleCoordToLod, fractional LOD remainders, world wrapping and map conversion in eg3dproj.c/stterr.c |
| Flight integration | Not complete | stepFlightModel forces, velocity/position integration, coefficients, tick scaling, clamps |
| Combat/AI | Not complete | Projectile fine positions, guidance, collision/proximity thresholds, movement and acquisition math |
| Randomness/time | Only a scaling helper | Separate deterministic RNG and simulation clock contracts from numeric representation |

Not every integer operation is fixed-point math. Object indices, packed flags,
table addressing, binary serialization and event counters remain integers.
Likewise, replacing a gameplay approximation with sqrt/hypot changes behavior
independently of whether the storage is fixed or floating point.

## Backend design

1. Freeze the current f15se2-ex behavior as the default compatibility baseline.
   Record intentional differences from the DOS executable separately. Existing
   C comparisons establish current-port equivalence, not DOS-binary equivalence.
2. Inventory numerical state and operations by subsystem. Specify units, axis
   order (including historical X,Z,Y paths), angle convention, valid range,
   intermediate width, rounding, saturation, wrap and invalid-result behavior.
3. Extract pure operations with explicit inputs/outputs. Put global state
   handling around them. Keep legacy narrowing at documented compatibility
   boundaries rather than scattering casts through new code.
4. Introduce a shared policy interface with backend-specific Angle, Scalar,
   Position, Vector and Matrix types. Use compile-time policies initially;
   select an instantiated simulation implementation at sortie start if runtime
   selection is needed. Avoid virtual calls for individual multiplies.
5. Keep legacy raw words in the fixed backend. Use double for initial modern
   simulation state; convert to float at renderer boundaries where appropriate.
   Preserve fractional position/velocity between ticks. Float calculations
   followed by stores into the same old integer globals lose the new precision.
6. Migrate one subsystem at a time, starting with pure trig/matrix/camera work,
   then orientation, flight integration, and finally guidance/collision/AI.
   Preserve formulas initially; change approximations or flight behavior only
   as separate, measurable changes.
7. Keep on-disk formats stable through explicit encode/decode adapters. Use
   backend-specific runtime state; quantize only when the selected legacy format
   requires it. Do not switch backends in the middle of a sortie initially.

The floating backend should improve precision, not emulate every truncation.
The fixed backend preserves old results. Their acceptance criteria differ.

## Required final state

The import is only a first step, not acceptance of the migration. Completion
requires both original fixed and modern floating-point implementations behind
the same high-level API, used throughout the game's numerical state and callers.

* Angles, positions, displacements, velocities, accelerations, durations, ratios,
  vectors and transforms use distinct types with documented units and frames.
  Primitive constructors and conversion operators must not allow implicit
  conversion. Incompatible quantities must fail to compile.
* Raw representation access belongs inside backend implementations and named
  boundary adapters. Public `raw()` methods available throughout gameplay would
  defeat this requirement. File decoding/encoding is an explicit exception;
  graphics, platform input and external APIs also require narrowly scoped adapters.
* Indices, flags, counts and identifiers remain ordinary integers. They must not
  be mistaken for simulation quantities. Existing C-style interfaces are temporary
  migration adapters, not permanent bypasses of the type system.
* Add compile-fail tests for primitive arguments, implicit extraction, unit/frame
  mixing and invalid operators. Add a CI check of simulation declarations and
  boundary access, using Clang AST analysis where syntax checks cannot distinguish
  identifiers from numerical state. Maintain an explicit, reviewed allowlist.
* Characterize callers before replacing them: camera/HUD, orientation, flight,
  guidance, targeting, collision, terrain and mission state. Compare the fixed
  path to a frozen baseline with tick-by-tick inputs and state, including rounding,
  overflow boundaries and failure paths. Existing production helpers are useful
  during extraction but cannot remain the only oracle after they are replaced.
* Run the same scenario suite on the modern backend with documented tolerances
  and outcome invariants. Modern state must retain fractional precision. Exact
  fixed/float equality is neither required nor desirable at every intermediate.
* Test original resource decode/encode separately, including unchanged round trips,
  out-of-range errors and explicit quantization rules for modern values. Never
  silently change the original file layout to match new runtime types.

Do not mark this work complete until all numerical subsystems are inventoried,
migrated and tested; a backend selector over a few scalar helpers is insufficient.

## Verification

Current import checks:

* `fixed_math_tests`: imported standalone tests plus independent signed-limit
  regression cases. Several inherited expected-value helpers mirror the old
  formulas, so these alone are not an independent correctness oracle.
* `fixed_math_backend_tests`: links the real current core. Exhausts all 65,536
  angles for sine and cosine; checks signed-word boundary products, ranges,
  bearings and scaled trig; checks six 32-bit values at every shift count 0..31;
  compares rotation matrices and their squares for 729 angle triples.
* Full Linux Release build and all 40 registered CTests passed after the import.
  The subsequently expanded matrix comparison also passed.
* Clang 18 static analyzer on the standalone test translation unit: no
  diagnostics. Clang ASan/UBSan with nonrecovering errors: passed. GCC C++17
  UBSan with nonrecovering errors: passed. These checks cover exercised paths,
  not every possible input to every helper.
* Windows, Android and browser builds were not run for this import. Optional
  external-asset validation tests were not registered in this build.

Before further production routing, expand tests to all helpers and their input domains,
especially inverse-trig endpoints, inverse matrices, projection/clipping and
large scalars. Use fixed-seed generated cases and independent widened-integer
references. Keep multiplication-result narrowing explicit in adapters: the
library can return +32768 where a production signed-word store wraps it.

For whole-sortie migration, capture a fixed seed, initial state and tick-indexed
inputs. Compare fixed-backend state after each tick exactly. For floating point,
use declared angular/position/velocity tolerances, finite-value checks, matrix
orthogonality and scenario-level outcomes. Cover takeoff, turns, stall,
landing, missile pursuit, wrap boundaries and long flights. Rendering FPS must
not determine simulation step count or outcomes. Do not claim that a handful
of matching scalar tests guarantees identical missions.

Reproduce standalone Clang checks from the repository root:

```sh
clang++ --analyze -std=c++17 -Isrc -Xanalyzer -analyzer-output=text tests/fixed_math_tests.cpp
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc tests/fixed_math_tests.cpp -o build/math-clang-sanitized
./build/math-clang-sanitized
ctest --test-dir build --output-on-failure
```
