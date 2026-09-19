# Math semantics and inherited limits

This is a source-based, incomplete audit, not a claim that the modern backend
has a physically validated flight model. Floating-point storage alone is not
modernization. Numerical representation, resource encoding, control tuning and
aircraft capabilities must be separate concerns. No units library is required.

## Evidence and decisions

| Source | Observed meaning | Classification and action |
| --- | --- | --- |
| `src/math/rotation.hpp`, `src/math/flight_control.hpp` | 65536 angle words form a turn; roll/pitch command units correspond to 128 angle words per second | Representation conversion. Keep word arithmetic in fixed math; modern commands are angular rates in radians per second. |
| `src/math/altitude.hpp::constrain` | Low-word checks and a 60000 flight-altitude ceiling | Original compatibility behavior, not a representational necessity for doubles. Fixed keeps both; modern retains the terrain floor but no implicit ceiling. A future aircraft envelope must be an explicit policy. |
| `src/math/altitude.hpp::renderHeight` | Altitude above 8192 is compressed by two, above 16384 by four | Nonlinear scene-coordinate mapping, not a unit conversion. Retained for compatibility with scene/terrain consumers. Modern physics must eventually stay in uncompressed altitude and apply this only at scene boundaries. |
| `src/math/airspeed.hpp::constrain` | Fixed speed whose low word exceeds 45000 is reset to zero | Legacy validity rule. Modern no longer caps positive speed at 45000; it retains a nonnegative magnitude. This does not establish that other speed consumers support unlimited range. |
| `src/math/airspeed.hpp` | Flight speed uses 27 units per indicated knot | Existing engine scale. Conversion to physical velocity still needs verification against distance and simulation time; do not label these values metres/second. |
| `src/math/propulsion.hpp::targetSpeed` | 899-knot target ceiling, altitude/fuel/load factors | Aircraft performance approximation and tuning, not integer-width necessity. Still inherited by modern math; must become explicit aircraft-model policy rather than simply deleting the ceiling. |
| `src/math/aerodynamics.hpp::loadResponse` | Maximum load 128 sixteenths of a G (8 G) | Gameplay/aircraft envelope. Still inherited; separate from arithmetic precision and validate against aircraft behavior. |
| `src/math/aerodynamics.hpp::turnRate` | Quantized speed denominator and empirical turn scale | Mixed representation and model approximation. Modern removes quantization but has not replaced or physically validated the model. |
| `src/math/guidance.hpp::recoveryApproach` | 30/64 map-unit offsets, 28/56 aim offsets, distance-dependent height capped at 4096, carrier addition 100, corridor target -20 | Guidance tuning in mixed map/scene scales. Their computational roles are known, but physical rationale is not established. Needs a documented approach profile, not renamed literals or unrestricted values. |
| `src/math/guidance.hpp::recoveryAttitude` | Division by 8, trim conversion, angular-rate clamps | Controller gains and authority limits. Keep fixed reference; modern needs explicit gains with coordinate/time semantics and response tests. |
| `src/math/horizontal.hpp` | Fine view coordinates and inverted map Y | Coordinate convention. Keep distinct from coarse `MapPosition`; resource/world conversion boundaries still need consolidation. |

## Acceptance for further migration

1. Trace each quantity to its producer and consumers, including file and display
   boundaries. State the known scale, sign, frame, time basis and uncertainty.
2. Freeze original caller behavior independently before changing that caller.
3. Keep original rounding, wrapping and legacy rules in the fixed reference.
4. Give modern policy parameters meaningful quantities and explicit defaults.
   Do not silently convert an unexplained number into a supposed physical fact.
5. Test modern range, fractional precision, dimensional relationships and
   scenario outcomes independently of fixed-bit parity.
6. Verify whole-sortie behavior and platform support before declaring migration
   complete. Current helper tests do not prove this.

This first pass covers selected migrated flight helpers. Radar, weapons, AI,
projection, terrain, camera, file formats and remaining scalar callers still
require the same audit. The two removed modern ceilings are not an assertion
that all original game boundaries have been removed.
