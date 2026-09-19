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

## Traced scale relationships

### Flight altitude, displayed altitude and velocity

`src/eghudr.c` reads `altitudeUnits(g_altitude)` directly into the altitude tape,
divides by 1000 for its labels, and describes the low-altitude branch as
"altitude < 1000 ft". This establishes the display's feet convention, not the
physical correctness of every simulation formula. The tape also narrows to
`uint16`, so removing the modern altitude ceiling does not fix the display.

The current flight relationship is traceable without guessing units:

1. `src/egflight.c` assigns `g_knots = speedWord(g_velocity) / 27`.
2. `AltitudeMath::climb` divides the velocity sample by 10 before applying the
   flight-path sine (with extra fixed rounding in the fixed backend).
3. `AltitudeMath::integrate` integrates that rate with a simulation step;
   `ControlBoundary::frequency` maps frequency to `1/hz` seconds in modern math.
4. Thus, ignoring fixed rounding, a speed sample representing K indicated knots
   generates `2.7 * K * sin(flightPath)` flight-altitude units per simulation
   second. Horizontal integration uses the same velocity / 10 factor in fine
   view coordinates (`HorizontalMath::component`).

This is an observed engine relationship. It does not justify calling the speed
sample metres/second, nor treating coarse map units as feet. Before substituting
a physical velocity conversion, establish map-to-fine-coordinate scale, intended
simulation time scale, and effects on stall, targeting, collision and travel time.
Keep the existing relationship in the fixed reference. Modern physical scaling
needs explicit adapters and trajectory tests, not scattered replacements of 27
or 10. The modern render-height compression is also still a legacy scene bridge.

### Input precision versus controller tuning

`FlightControlMath::fromJoystick` shifts each byte right by four before either
backend branch. The modern path therefore still loses those four bits. Its
neutral region is byte values 112..143, roll shaping is quadratic, and pitch
authority is asymmetric. These are three distinct issues:

* Byte/nibble quantization is representation loss.
* Neutral region is input deadzone policy, separate from SDL calibration.
* Response curve and maximum angular rates are controller tuning.

`tests/typed_flight_control_tests.cpp::fixedInputs` currently requires the modern
response to match those same quantized rates for all byte pairs. That is a
legacy-profile test, not a modern-precision acceptance test. Retain fixed
exhaustive coverage, then add a normalized analog input type and an explicit
response profile with deadzone and angular-rate semantics. Test adjacent analog
samples outside the deadzone, center stability, saturation, asymmetric pitch
limits and frame-rate-independent integration. Trace the SDL-to-byte boundary in
`src/joystick.c` as well; improving only the final curve cannot recover input
precision already discarded there. No production input behavior changes in this
audit checkpoint.

The first implementation step adds `AnalogStick` and `AnalogResponse` to
`flight_control.hpp`. `fromAnalog` accepts normalized input and explicitly typed
maximum roll/positive-pitch/negative-pitch angular rates, plus a dimensionless
deadzone fraction. It linearly rescales the range outside the deadzone; this is
an explicit new controller profile, not a claim to reproduce the old curve.
There is no built-in aircraft rate or deadzone default. Positive input maps to
positive command, so device inversion belongs at the input adapter.
All 65536 signed-axis samples are checked for monotonicity and numerical
response, including asymmetric pitch limits. Invalid normalized inputs and
deadzone profiles are rejected. Compile-failure tests protect raw construction,
axis identity and backend identity. The byte-based `fromJoystick` remains the
legacy response path.

`joy_physicalStick` now reads the active SDL device into `AnalogStick` before
byte conversion. Uncalibrated signed endpoints map separately to -1 and +1;
raw-device calibration divides directly by each saved center-to-endpoint span,
without the integer intermediate in `joy_correctAxis`. Values outside calibrated
travel saturate at the endpoints. A missing device returns center. This adapter
does not apply a deadzone, response curve, button overrides, focus policy or
replay policy. It must not replace the flight reader until those responsibilities
are integrated and tested. The existing byte reader remains unchanged in
behavior, with a separately committed SDL characterization baseline.

Virtual-device tests cover both device paths, deadzone-edge samples without
modern snapping, signed endpoints, one-count motion around displaced calibrated
centers, saturation and missing-device centering. Production flight selection
and recording of continuous samples are still pending; existing byte recordings
cannot reconstruct their lost precision.

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
