# Fixed and floating-point math migration

The [semantics audit](math_semantics_audit.md) distinguishes representation from
physical meaning and gameplay policy. Existing modern helpers remain provisional:
floating-point versions of legacy formulas are not sufficient evidence of a
modern flight model. Modern altitude/speed constraints no longer inherit the
60000/45000 ceilings; fixed behavior retains its original checks.

Status: 2026-09-19. Typed fixed and double-precision rotation backends are
implemented in `src/math/rotation.hpp`. Production matrix builders, matrix
multiplication, camera rotation, persistent aircraft orientation matrices and
attitude recovery now use the typed fixed implementation. Aircraft Euler state and
its camera snapshots are typed as well; render interpolation has fixed and modern
implementations. Roll/pitch command storage, joystick shaping, ground steering
and control-rate integration now use typed flight-control math as well.
Flight altitude and climb-rate storage, vertical integration and altitude display
mapping also have typed fixed/modern implementations.
Persistent fine X/Y coordinates, horizontal movement, landing easing and camera
snapshot interpolation now use axis-specific types.
This is not a completely migrated simulation. An experimental build-time selector
now chooses the backend for persistent flight state and migrated calculations.
The default remains fixed. Configure a separate build directory with
`cmake -S . -B build-modern -DCMAKE_BUILD_TYPE=Release -DF15SE2_MODERN_MATH=ON`,
then build target `f15se2` and run `build-modern/f15se2-ex-modern --game <assets>`.
The window title identifies the experimental build. Blackbox build identity has
a modern suffix to prevent accidentally accepting a fixed recording as matching.

`GameBackend` selects the types of attitude, orientation matrices, altitude,
speed, position, thrust, load and control globals at compile time. No per-frame
copy back into fixed state is required. Legacy adapters still quantize values
for unmigrated consumers, including render/file words and several gameplay
decisions. The byte input response remains in use, not the continuous physical
stick adapter. This is an experimental modern flight path, not proof that all
original limits have been removed or that combat/AI/rendering are modernized.

The fixed build retains the complete fixed-reference suite. The modern build
registers `modern_flight_smoke_tests`, which links its real core, statically
verifies modern state types, checks persistent fractional orientation and the
removed altitude/speed cutoffs, and executes 120 production flight steps.
This smoke test is not a completed sortie or full modern behavior coverage.

The production `advanceFlightOrientation` now delegates ordered matrix updates
to `FlightControlMath<B>::advanceOrientation`. Fixed and modern specializations
share body-roll/body-pitch/world-yaw order and skip zero increments. The fixed
caller retains scratch-matrix and refresh-counter behavior, checked against its
pre-existing scalar oracle. Modern tests retain the matrix over 12,000 sub-word
steps and check mixed-axis multiplication against explicit reference matrices.
The selected game backend uses this shared integration path.

## Typed recovery approach geometry

The production recovery path now uses `GuidanceMath::recoveryApproach` with
coarse `MapPosition` values, distinct from fine view positions and render heights.
It returns typed bearing/height plus slow-motion and brake-policy decisions.
Fixed geometry preserves signed-word stores, the unusual wrapped clamp and
legacy bearing approximation. Modern geometry uses continuous coordinate
differences and `atan2`; fractional coordinates, translation invariance and
large aim offsets are tested explicitly. Existing gameplay height limits remain
policy, not numerical range limits. The caller still owns target selection,
actual brake activation, gear changes and ground-contact handling.

Raw map construction is confined to the reviewed `legacy_map.hpp` adapter;
boundary-check regression tests reject its use from unreviewed game files.
Nine recovery compile-failure cases reject raw map construction, fine/coarse
coordinate substitution, backend mixing, raw direction/speed arguments,
flight-altitude/render-height substitution, and implicit thrust extraction.
A valid modern recovery call chain is compiled as a positive control.
Modern guidance tests and standalone Clang analysis/ASan/UBSan pass. This does
not establish modern full-sortie behavior or every fixed map edge case.

## Large-offset recovery baseline

Before changing approach geometry, the real-caller recovery grid is expanded
from 15,552 to 43,200 cases against `5ae61c7`, adding +/-16000 map-unit offsets
on both axes. This exercises signed-word narrowing of lateral aim points and
arguments to the bearing calculation, as well as the legacy clamp's wrapped
distance inputs. The independent oracle explicitly narrows at those boundaries.
The full-flight characterization test passes and Clang analysis of its test
translation unit reports no diagnostics. This is bounded coverage, not every
possible map coordinate; exact INT16_MIN bearing components and touchdown
remain outside the fixture. No production code changes in this checkpoint.

## Typed recovery thrust

Recovery throttle generation now returns `EngineThrust` from typed bank target
and approach height. Its fixed implementation preserves the two independent
integer divisions and 35..80 command limits; modern math retains fractions.
The intermediate bank target no longer leaves the typed API. The requested
throttle still crosses an explicit adapter into `g_setThrust`, whose remaining
consumers have not been migrated. The helper is checked against 262,144 fixed
reference combinations and a fractional modern case, in addition to the
previously committed full-flight recovery fixtures.

## Typed recovery bank target

`GuidanceMath::recoveryBank` now computes the speed-dependent bank target from
typed bearing, heading and flight-speed quantities. The caller adapts its existing
indicated knots to the 27-units-per-knot scale; it deliberately does not sample
the newly integrated velocity. Fixed math preserves the 16-knot buckets and
signed-word result; modern math retains fractional speed. Negative indicated
speed is rejected. The corridor override returns level bank. The typed target
feeds recovery steering directly, with a scalar extraction retained only for
the not-yet-migrated throttle policy. Tests cover 524,288 fixed angle/speed
combinations and their corridor overrides, plus modern fractional speeds.

## Typed recovery attitude

Recovery roll/pitch command generation now uses `GuidanceMath::recoveryAttitude`
with typed scene heights, attitude, bank target and trim. Fixed math preserves
the separate signed-word and floor-division steps; modern math retains fractional
inputs with equivalent command limits. Approach geometry, bearing generation,
throttle policy and touchdown handling remain at the legacy caller boundary and
are not yet migrated. The new helper has 262,144 independent fixed-reference
cases plus modern fractional-input checks. Standalone Clang analysis and
ASan/UBSan pass; the previously committed airborne recovery caller fixtures
also pass with the production routing change.

## Recovery caller characterization

Before migrating recovery guidance, the full-flight test adds 15,552 airborne
fixtures against `bf7600a`. They span runway/carrier targets, north/south signs,
absent/matching/wrong-target recovery corridors, heading boundaries (including
511/512, 16384/16385 and -32768), lateral/longitudinal offsets, both frame rates,
three bank angles and speeds surrounding the 350-knot gear threshold.
An independent frozen scalar bearing and guidance reference checks resulting
roll/pitch commands, requested throttle, gear/brake flags and slow-motion state.
The real flight model and virtual joystick are used, without math stubs.
The expanded test passes; production code remains unchanged. Touchdown,
nonzero initial pitch/trim, extreme map coordinates and full sorties remain
outside this recovery fixture's coverage.

## Heading-wrap caller characterization

The full-flight characterization test now exercises normal altitude hold at
heading words 0, 1, -1, 32767 and -32768. Its independent scalar oracle includes
heading subtraction before signed-word narrowing and starts the expected
orientation matrix at the actual heading. This expands altitude-hold coverage
from 864 to 4,320 cases while preserving the original 1,166,400 manual-flight
cases. Production code is unchanged in this checkpoint.

The expanded caller test passes against `3980833`; Clang static analysis of the
test translation unit reports no diagnostics. This does not characterize runway
or carrier recovery guidance, ground contact, or complete recorded sorties.

## Blackbox integration checkpoint

The deterministic-blackbox branch is integrated with the current typed state.
Diagnostic hashes and JSON snapshots explicitly extract legacy recording units;
they do not cast quantity objects or weaken the production type boundaries.
Renderer test fixtures use named members to accommodate replacement-model metadata.

This is recording infrastructure, not yet a whole-sortie math acceptance gate.
Before using recordings to certify migration, complete these checks:

* Audit replay isolation for current pointer, touch, UTF-8 and calibrated joystick
  paths, which are newer than the imported recorder's BIOS-key/axis schema.
* Cover replacement/compressed intro audio waits in deterministic execution.
* `blackbox_replayFailed()` now reports observed core/diagnostic mismatches and
  unconsumed checked streams at shutdown. Its sticky value survives repeated
  shutdown and resets for a new session. Tests cover matching/mismatched markers,
  extra RNG consumption and each checked unconsumed stream. This is not a
  completion verdict: inspection pauses, omitted optional diagnostic streams,
  axes coverage and rejected startup must be handled by an acceptance runner.
  Wire that runner and process exit status before treating replay as a CI gate.
* Extend state coverage beyond the imported selected-field hashes and compare
  actual recorded sorties before and after migration. Add declared tolerances
  and outcome checks for the modern backend rather than requiring identical hashes.
* Verify Windows, Android and browser behavior separately from Linux unit tests.

## Tests-first gate for further migration

`GuidanceMath::altitudeHold` now implements the normal altitude-hold commands
for both backends. The fixed caller uses typed scene heights, attitude, bearing,
heading offset and trim, and returns typed roll/pitch commands. Mission-tick
offset selection stays in the caller; recovery-waypoint steering is not migrated.
Fixed guidance preserves word wrapping and floor division without negative
left shifts. Modern guidance uses shortest-arc heading/roll errors, fractional
scene-height error and radians-per-second outputs, with the same command limits.
The new unit grid checks 1,310,720 fixed combinations, fractional modern control,
limits and heading wrap. Compile checks reject scalar input, expanded flight
altitude substituted for scene height, and mixed backends.
Verification: the Linux Release build and all 58 CTests pass. Clang analysis and
ASan/UBSan pass for the guidance harness. Full-flight characterization also passes
ASan/UBSan with `egflight.c` instrumented (the rest of the linked core is not).
Windows, Android, browser and recorded whole-sortie verification remain open.

Altitude-hold caller checkpoint (baseline `0e5cbc2`): 864 additional targeted
full-flight cases cover neutral-stick altitude hold at two rates, six heights,
three initial pitches and six banks. Four scenarios exercise small positive and
negative altitude/bearing errors, signed-angle trim limits, bearing wrap and the
engaged-autopilot mission-tick heading offset. Independent formulas check roll
and pitch commands after load limiting plus the full rotation matrix sequence.
All 1,167,264 characterization cases pass and Clang analysis of the harness is
clean. Production autopilot is unchanged at this checkpoint. Recovery-waypoint
steering, input cancellation and Android overrides need separate coverage.

Aerodynamic yaw is now implemented by `AerodynamicsMath::turnRate` for fixed and
modern backends. The caller keeps `YawRate` typed through ground steering and
orientation integration; only Android diagnostic output extracts its old units.
The fixed implementation preserves the unsigned speed-word band, two rounded
trigonometric products and intermediate signed-word division result without
left-shifting negative values. The modern implementation keeps fractional load,
speed and trigonometry and returns radians per second without word wrapping.
Its denominator must be positive and its result finite.

The dedicated unit grid checks all 65,536 angles at ten speed boundaries and six
loads (3,932,160 cases); modern checks cover fractional rates and speeds beyond
the legacy word range. Compile-negative tests reject raw operands, backend
mixing, fuel-as-flight-load and yaw-as-pitch assignments. The full-flight yaw
baseline below remains in place. This does not enable a whole-game modern backend.
Verification: Linux Release build and all 57 CTests pass. Clang analysis of the
aerodynamic harness is clean. ASan/UBSan pass for both the aerodynamic harness and
full-flight characterization with `egflight.c` instrumented; the remaining linked
core is not instrumented. Windows, Android and browser checks remain outstanding.

Aerodynamic-yaw caller checkpoint (baseline `742431d`): the full-flight
characterization harness now compares `g_matrixScratch` against a frozen scalar
yaw formula and independent matrix products in all 1,166,400 existing cases.
The assertion observes the pitch-then-world-yaw rotation before a later stall
correction can rebuild the attitude matrix. It preserves both signed-word yaw
stores, rounded lookup-table products and post-braking speed sampling. Production
yaw remains unchanged at this checkpoint. This grid starts at speed 8100 and
uses neutral roll input; it is not exhaustive coverage of speed, ground steering,
Android attitude override or simultaneous nonzero roll commands.

Before changing each remaining production path, add characterization tests that
run that unchanged path, verify them, and commit the tests separately. Keep their
fixed-backend expectations unchanged through migration. Add separate modern
precision and outcome tests; helper-only coverage does not establish caller parity.

`flight_model_characterization_tests` established a baseline against `dbfb4ab`
without production edits or math/flight-model test doubles. Its initial 48,600 independent
ticks cover thrust ramp-up/down, damage caps, fuel burn cadence and exhaustion,
load factor (including its high-bank cap), corner/stall thresholds, and target-speed
to acceleration ordering at 4 and 15 Hz. Inputs include three heights, three
pitch angles and six bank angles. Expected values are frozen integer expressions
with explicit word stores and signed shifts, not calls to the new backend.
The expanded harness also passes ASan/UBSan with `egflight.c` instrumented; the
rest of the linked core is not instrumented. Clang analysis of the harness is
clean. This is bounded caller coverage, not full flight-model coverage: turn
trajectories, assists, ejection, landing, multi-tick sorties and the remaining
branches still need baselines before their migration.

Before target-speed migration, a tests-only extension against `7e9e228` expands
this matrix to 194,400 ticks by crossing gear-up/down and airbrake-on/off states.
It preserves the existing expected values and adds gear drag, post-acceleration
braking, and lift/trim assertions. In particular, lift must sample accelerated
speed before airbraking, and trim must use the initial roll. These cases still
use neutral controls and do not establish coverage of autopilot or input paths.
The Linux Release build, all 52 CTests, Clang analysis of the harness, and ASan/UBSan with
`egflight.c` instrumented pass for this extension; the rest of the core remains
uninstrumented. No units library is introduced: backend quantities remain
project-owned strong types.

## Applied-thrust migration checkpoint

The tests-first baselines are commits `c760498` and `a701aac`. After those
checkpoints, `g_thrust` becomes `EngineThrust<FixedBackend>` and the covered
damage limit and engine ramp use `PropulsionMath`. Full-tick expected results
are unchanged; only fixture construction and extraction use the new adapter.

The fixed ramp retains separate `/4` and `/frequency` truncations, then the
one-unit increment and immediate downward clamp. Named constants document
the 144-unit afterburner ceiling and four-unit damage penalty. These are game
command units, not newtons. Damage-limit notification retains the old predicate
even when severe damage leaves an already-zero command at zero.

The modern ramp exactly integrates `dT/dt = (target - thrust)/4 + 15` until
the target is reached. The additive term is calibrated to the current 15 Hz
simulation, while substeps retain fractions and give the same elapsed-time
result. Downward commands remain immediate. This is an explicit modern model
choice, not a claim of bit-for-bit equivalence to the fixed ramp.

`typed_propulsion_tests` checks every signed-word current value against eight
targets and four frequencies, plus nine damage counts. Modern tests check
subunit values, bounded response, elapsed-time equivalence, and invalid values.
Compiler tests prohibit raw construction/extraction, private storage access,
backend/quantity mixing, primitive timesteps and raw pointers. Raw propulsion
adapters are included in the boundary checker and its regression tests.

At this checkpoint, requested throttle (`g_setThrust`), fuel and target-speed generation remain
scalar. Applied thrust still has explicit transitional conversions for the
unmigrated target-speed formula and the audio interface. Input/autopilot paths
need their own pre-migration caller coverage before requested throttle changes.
No whole-game modern backend selector is available yet.

Verification: Linux Release build and all 52 CTests pass, including 69
compiler-negative cases and the positive control. Clang analysis of the
propulsion test is clean. ASan/UBSan pass for the standalone propulsion test
and the full-tick characterization harness linked with instrumented `egflight.c`
(the remaining core is uninstrumented). Windows/Android/browser builds and
interactive flight scenarios have not been verified for this checkpoint.

## Target-speed migration checkpoint

After the separate tests-first commit `de3ab99`, target-speed generation in
`stepFlightModel` uses `PropulsionMath::targetSpeed`. Its inputs are typed thrust,
pitch coefficient, compressed scene height, fuel load, load factor and a gear
enum; its result is `FlightSpeed`. Raw `speedCalc` and `targetVel` locals are gone.
The original flight-tick expected values are unchanged.

The fixed implementation preserves each signed-word store, rounded Q15 pitch
drag, unsigned scene-height interpretation, signed shifts and truncating fuel
division. The old signed-32-bit load-product overflow domain is rejected rather
than assigned invented parity. Named constants retain the 899-knot cap,
27 velocity units per knot and one-eighth extended-gear drag.

The modern implementation retains fractional fuel, load, scene height and speed
through the same scaling stages, without word wrapping or intermediate integer
quantization. It uses the same final speed limits and rejects non-finite results.
This is higher precision in the existing game model, not a new aerodynamic model.

Helper tests cover 64,512 fixed target-speed combinations, including signed-word
limits and quantization boundaries, and 864 modern combinations with an
independently arranged continuous reference. Eight additional compiler-negative
cases prohibit primitive quantities, extraction, private access, backend/role
mixing and raw gear flags; the valid API is compiled as a positive control.
Clang analysis of the propulsion harness is clean. ASan/UBSan pass for the typed
helper tests and the unchanged 194,400-tick baseline with `egflight.c`
instrumented; the remaining linked core is not instrumented.
The Linux Release build and all 52 CTests pass, including 77 compiler-negative
cases and the valid-API control.

Fuel/load storage and their producers, requested throttle, and corner-speed
generation remain transitional scalar code. Scene height is adapted from the
existing `g_viewZ`, not recomputed from flight altitude at a different update
stage. These adapters are explicit migration debt. Whole-game backend selection,
multi-tick mission outcomes and Windows/Android/browser verification remain open.

## Tests-first gate for corner speed

A tests-only extension against `ac2fbf8` adds flight altitudes 16,383, 16,384
and 60,000 to the full-tick matrix, bringing it to 388,800 cases. Scene height
is independently initialized with the original compression formula. The oracle
uses that scene height for target-speed scaling but flight altitude for corner
speed, so substituting one altitude representation for the other is observable.
The tested production corner-speed calculation remains unchanged.

`fixed_math_backend_tests` now compares `integerSqrtCompatible` with the actual
`isqrt` entry point over all 65,536 signed-word inputs. Explicit checks preserve
the surprising results for -32,768, zero and eight (1, 1 and 3 respectively).
This establishes compatibility of the imported Newton helper, not equivalence
to a mathematical floor square root. The existing full-tick corner oracle uses
an independent integer root for its bounded load-factor samples.

Both affected tests build and all 52 CTests pass. Clang analysis of both harnesses is clean;
the expanded flight harness passes ASan/UBSan with `egflight.c` instrumented and
the rest of the core uninstrumented. No production behavior is changed by this
checkpoint. Negative-load, assisted-input and multi-tick corner-speed behavior
still require their own caller coverage.

## Corner-speed migration checkpoint

After tests-first commit `6ba1d94`, `stepFlightModel` calls
`AerodynamicsMath::cornerSpeed` and `stallThreshold`. `CornerSpeed` is a strong
type in indicated knots, distinct from the 27-units-per-knot flight and stall
speed types. The covered producer no longer performs scalar corner arithmetic
or reconstructs the stall threshold through a raw-number adapter.

The fixed backend preserves the unsigned 32-bit altitude product, signed-word
stores, legacy Newton root, arithmetic shift and final absolute-value store.
Signed native overflow in the load-times-four expression is rejected. The
modern backend uses the continuous square root of load magnitude and retains
fractional altitude, corner speed and stall threshold. At zero load its root
is zero, intentionally unlike the fixed helper's minimum-one result. Modern
altitude scaling avoids unnecessary multiply-before-divide overflow.

Tests compare 720,896 fixed combinations (eleven altitudes and all signed-word
load values) to frozen stage-by-stage arithmetic, plus modern fractional and
overflow cases. The previously committed 388,800-tick baseline is unchanged.
Seven new compiler-negative cases prohibit raw corner values/extraction,
altitude/load/backend mixing, flight-speed-as-corner conversion and raw pointers.
Clang analysis is clean; ASan/UBSan pass for the aerodynamic harness and the
full-tick baseline with `egflight.c` instrumented, not the entire linked core.
The Linux Release build and all 52 CTests pass, including 84 compiler-negative
cases and the valid-API control. Windows, Android and browser builds are unverified.

`g_cornerSpeed` remains a scalar compatibility store for unmigrated ground and
assist consumers, reached through the explicit `cornerKnots` adapter. Load
generation, negative-load caller scenarios, assisted controls and whole-game
backend selection remain open. No units library is used.

## Flight-load migration checkpoint

Tests-first commit `a875575` extends the unchanged flight model to 1,166,400
cases using an SDL virtual joystick for centered and both full-pitch positions.
The input pump overwrites keyboard-stick globals, so assigning those globals
alone does not exercise non-neutral input. The fixture checks the axes actually
consumed by the flight model. It also freezes the unusual ordered load clamp:
a negative pitch can make its lower and upper bounds inverted.

`AerodynamicsMath::loadResponse` now accepts typed bank load and pitch command,
and returns typed load and the limited pitch command. Fixed mode preserves the
separate truncating division, signed-word clamp arguments, and upper-bound-first
clamp ordering. Modern mode retains fractional pitch contribution and limiting;
it does not reproduce signed-word wrapping. Both retain the eight-G ceiling.
The caller reuses the typed result for corner speed and propulsion drag.

The bank lookup and `g_gees` storage/formatting/yaw consumers remain legacy
boundaries; this is not a completed migration of load throughout the game.
Unit coverage exhausts all 256 bank-byte values, all signed 16-bit pitch
commands, and both airborne states (33,554,432 combinations). Modern tests
check fractional contribution, ground behavior, and fractional limiting.
Four added compile-negative cases reject primitives, wrong axes, wrong
quantities, and backend mixing at the new API.

Verification: full Linux Release build and all 52 CTests pass. Clang analysis
of the aerodynamic harness is clean. ASan/UBSan passes for the aerodynamic
harness and full flight-model characterization, with `egflight.c` instrumented;
the rest of the linked core is not instrumented. Whole-sortie blackbox replay,
Windows, Android, and browser verification remain outstanding.

## Bank lookup and load storage checkpoint

After the load-response checkpoint, `g_gees` becomes `FlightLoad<FixedBackend>`.
The stored quantity can no longer be assigned a primitive or used directly in
integer arithmetic. Display/debug output and the pending yaw migration retain
one explicit legacy conversion; those consumers are not yet modern-backend
ready.

`AerodynamicsMath::bankLoad` takes a typed angle and a size-checked 128-byte
legacy table. Fixed mode preserves the original magnitude, 256-word binning,
and masked index, including the half-turn alias to bin zero. Modern mode
interpolates the same data instead of guessing a replacement aerodynamic
formula. Its last bin interpolates into bin zero at the half-turn boundary.
This deliberately changes fractional modern results, not fixed results.

The committed full-flight baseline from `a875575` covers the migrated caller;
only explicit result extraction changes in that harness. Unit tests additionally
compare every fixed angle word against the frozen lookup expression, and check
modern bin fractions, positive/negative symmetry and half-turn wrap. Additional
compile-negative cases reject primitive angles, backend mixing, wrong table
sizes, and primitive assignment/extraction of load.

The full Linux build and all 52 tests pass at this checkpoint. Clang analysis
of the aerodynamic harness and both scoped ASan/UBSan harnesses pass, with the
same instrumentation limits described above. No whole-sortie or cross-platform
parity claim is made.

## Rotation migration checkpoint

* `Angle`, `Coefficient`, `EulerAngles` and `Matrix3` carry backend types. Storage
  is private; primitive construction/extraction and mixed-backend operations
  are not public APIs. Forty-one negative compilation tests enforce representative
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
* `g_orientMatrix` and its product scratch value are now `Matrix3<FixedBackend>`,
  not word arrays. `applyRotationDelta` accepts typed matrices only. The three
  global per-axis arrays have been removed; typed axis-delta builders preserve
  their exact fixed coefficients, including the unchanged-axis 32767 value.
* Recovery is a pure backend operation returning typed Euler angles and a refresh
  request. Fixed recovery retains the old interpolated inverse trig, quotient
  word wrapping, quadrant rules, pole fallback and dirty bands. Modern recovery
  uses atan2/hypot, with roll zero at a pole; it does not apply legacy refresh
  bands. It is not yet wired into a selectable modern flight simulation.
* `tests/math_attitude_reference.hpp` freezes pre-migration recovery from
  `4d0efd4`. Differential tests cover 65,536 generated orientations and all signed
  pitch-component inputs; all 65,536 angle words are checked for each axis delta.
  Caller tests cover 4,096 persistent updates and 8,192 roll/pitch/yaw update,
  recovery and refresh cycles. Existing original-behavior assertions remain;
  their raw fixture arrays now enter through explicit test boundary adapters.
* `g_ourHead`, `g_ourPitch` and `g_ourRoll` now store `Angle<FixedBackend>`. Recovery
  assigns typed values directly, rebuild reads typed values, and camera snapshot
  capture/restore does not pass through integer angles. Primitive assignment,
  updates and taking an integer pointer to an angle are rejected by the API.
* `PoseInterpolation` retains fixed shortest-arc truncation and whole-pose gimbal
  snapping. Its `FrameFraction` validates scheduler intervals before camera state
  changes. The modern implementation retains fractional radians. Modern angle
  arithmetic normalizes to [-pi, pi), including a consistent half-turn value.
* Unmigrated HUD, targeting, combat, map and flight scalar formulas read typed
  Euler state through explicit `legacy::signedAngle` adapters. Eleven consumer
  files were checked to differ only by these read adapters and includes. These
  allowlisted bridges are migration debt, not completed typed domain math. The
  Android pointer-based attitude API is isolated behind `updateAttitudeFromWords`;
  its adapter is tested on desktop, not on Android hardware.
* Euler tests exhaust all 65,536 word values with twelve interpolation offsets
  each, and check signed extraction, wrap updates, coherent snaps, modern
  sub-word interpolation, platform input/output and invalid interval rejection.
  The original resource structures and file layouts are unchanged.

Verification at the Euler checkpoint: Linux Release build and all 44 CTests pass.
Clang analysis of the typed test translation unit reports no diagnostics.
ASan/UBSan passes for that instrumented translation unit and its inline math;
the linked production core was not sanitizer-instrumented. Windows, Android,
browser, live-flight and external-asset validation were not run in this checkpoint.

## Flight-control migration checkpoint

* `flight_control.hpp` adds axis-specific `RollCommand`, `PitchCommand`, `YawRate`
  and backend-specific `SimulationStep`. Commands cannot be interchanged across
  axes/backends or used as angles, primitive numbers or writable integer pointers.
  `g_rollInput` and `g_pitchInput` now use these types. Existing control producers
  outside the extracted math use named, read-only legacy adapters temporarily.
* Fixed storage keeps roll at 32 bits and pitch/yaw at 16 bits. Roll scales by
  128 in 32 bits before dividing by the tick frequency; pitch narrows to a signed
  word before division. Yaw divides its signed word directly. These deliberately
  different rules are frozen from `66ce785`, not generalized into one formula.
  Undefined signed shifts are expressed as multiplication and explicit modular
  decoding. Extreme overflow tests specify the intended word behavior, not a
  guarantee about the old undefined C++ expressions on every compiler.
* Modern rates use radians/second and positive steps up to one second. They retain
  fractional increments and do not reproduce fixed command overflow. Both backends
  currently keep the original joystick bins/deadzone/response curve; smoother
  input shaping is a separate gameplay change. Non-finite rates, invalid steps
  and overflowing modern rate addition are rejected.
* `stepFlightModel` uses typed increments and `advanceFlightOrientation`. The latter
  preserves right-multiplied body roll/pitch, left-multiplied world yaw, zero-delta
  skipping, rotation counters and attitude recovery. Android control pointers
  pass through a tested explicit adapter; Android hardware was not exercised.
* `typed_flight_control_tests` checks all 65,536 joystick pairs, every signed-word
  command at nine tick frequencies, wide roll boundaries, ground steering,
  platform input/output, and 4,096 production orientation-update cases against
  independent frozen expressions and the existing matrix/recovery references.
  Modern tests cover sub-word integration over 12,000 steps, units, intended
  divergence from fixed overflow and invalid inputs.
* This is not a whole-`stepFlightModel` replay test: autopilot, lift/thrust, damage,
  stall and position integration still need frozen tick-by-tick scenario coverage.
  Existing tests are retained, not replaced by the new control tests.

Verification: Linux Release build and all 45 CTests pass. Clang analysis of
`typed_flight_control_tests.cpp` reports no diagnostics; its ASan/UBSan run passes.
Only that translation unit and inline math are instrumented, not the linked core.
Windows, Android, browser and live-flight verification remain outstanding.

## Altitude migration checkpoint

* `altitude.hpp` separates flight altitude, vertical climb rate, terrain height,
  compressed render height and sampled airspeed. These use the existing engine
  scales; they are not advertised as metres or metres/second. Persistent
  `g_altitude` and `g_climbRate` are typed. Neither implicitly exposes a primitive
  nor accepts one, and scene heights cannot be assigned to flight altitudes.
* Fixed climb retains division of unsigned-word airspeed by ten before Q15
  multiplication and rounding. Integration keeps unsigned 32-bit altitude and
  signed-word climb-rate truncation. The low-word terrain/0xf230 check, 60000
  ceiling, piecewise render compression, unsigned landing easing and 500-unit
  obstacle escape retain their existing behavior, including wrapping.
* Modern climb and integration retain fractions. Descending below terrain clamps
  to terrain instead of wrapping; values above the ceiling clamp to 60000 instead
  of applying the legacy low-word check. Scene terrain height is expanded before
  comparing it to modern flight altitude. Current `egframe.c` assigns ground
  heights of 0 or 128, where both scales coincide; tests also check the modern
  inverse mapping above the compression thresholds. The ceiling remains the
  upper limit even if an unsupported terrain height exceeds it.
* `advanceFlightAltitude`, called by `stepFlightModel`, runs climb calculation,
  optional integration, constraints and render mapping. Automatic landing skips
  integration but still updates climb and constrains/maps the altitude. Landing
  easing and obstacle escape in `updateFrame` use typed operations. Crash/landing
  side effects and the rest of `updateFrame` are not yet migrated or replay-tested.
* HUD, targeting and the corner-speed formula temporarily use named read-only
  adapters. Airspeed and terrain state are sampled through explicit boundaries;
  their underlying global storage, horizontal movement and aerodynamic forces
  remain unmigrated. No original resource structures changed.
* `typed_altitude_tests` freezes expressions from `aa28521`: all 65,536 low-word
  altitudes, all angle words at eight airspeeds, all speed words at generated
  angles, full signed climb-rate inputs at five frequencies and eight altitude
  boundaries, landing easing and 24,000 production vertical-update cases.
  Modern tests check fractional climb and 12,000 accumulated subunit updates,
  compression thresholds, terrain/ceiling constraints and invalid inputs.

Verification: Linux Release build and all 46 CTests pass, including 31 negative
compiler cases with a positive control. Clang analysis of `typed_altitude_tests.cpp`
reports no diagnostics and its ASan/UBSan run passes. The linked production core
is not instrumented. Windows, Android, browser and live-flight checks were not run.

## Horizontal movement checkpoint

* `horizontal.hpp` separates fine view-frame coordinates from displacements and
  horizontal speed. X and Y are different types; the view frame retains the
  existing inverted map-Y convention. Primitive assignment, coordinate addition,
  axis mixing and fixed/modern mixing are compile errors. `g_ViewX` and `g_ViewY`
  now hold these coordinates; they are not integer aliases or writable proxies.
* Fixed movement keeps signed-word horizontal speed, rounded Q15 multiplication,
  division by ten, then tick-frequency division. Coordinate updates and landing
  easing explicitly decode 32-bit wrapping. Ordinary-domain results preserve the
  existing formulas; overflow tests define the intended machine-width behavior
  instead of relying on formerly undefined signed C++ overflow.
* Modern movement stores double-precision fine coordinates and retains fractional
  steps. It does not wrap at signed 32-bit limits. These are still engine fine
  units, not an assertion of an SI conversion. The velocity-to-horizontal-speed
  producer is now typed as described in the airspeed checkpoint below.
* `advanceFlightHorizontal` is the production movement caller. It skips movement
  during automatic landing. Map slewing uses typed displacements, and landing
  easing uses a typed operation. Old target/camera/terrain/render consumers have
  explicit read-only boundary adapters; their formulas are not yet typed math.
  The raw view-history ring still receives explicitly encoded coordinate values.
* Camera snapshots capture/restore typed X/Y directly. Fixed interpolation keeps
  truncating signed differences; modern interpolation retains fractions. A fixed
  interpolation product exceeding int64 is rejected before any live camera state
  is changed. The original camera/object tests remain and include a regression
  for this failure path. Object/projectile snapshots are still legacy scalars.
* `typed_horizontal_tests` freezes the expressions from `6bc4cc2`: all 65,536
  headings at eight speeds and five tick frequencies, signed-coordinate boundary
  addition/subtraction, landing easing, interpolation and 24,000 persistent
  production movement steps with automatic-landing pauses. Modern checks cover
  12,000 subunit steps, fractional snapshot/easing results and no 32-bit wrap.

Verification: Linux Release build and all 47 CTests pass, including 41 negative
compiler cases with a positive control. Clang analysis of `typed_horizontal_tests.cpp`
reports no diagnostics; its ASan/UBSan run passes. Only the test translation unit
and inline math are instrumented, not the linked production core. Windows,
Android, browser and live-flight verification remain outstanding.

## Airspeed checkpoint

* `FlightSpeed<B>` is persistent native-width velocity state, separate from
  `Deceleration<B>`, signed horizontal speed and unsigned vertical speed samples.
  `g_velocity` is now `FlightSpeed<FixedBackend>`; primitives cannot be assigned,
  extracted or passed by writable pointer. Initial conditions and projectile,
  HUD, stall and lift consumers use explicit transitional adapters.
* `AirspeedMath` implements acceleration, ground/air braking, carrier stopping,
  speed limits and typed horizontal/vertical samples. Fixed acceleration keeps
  signed 32-bit storage and the original two divisions. Ground/air braking,
  carrier stopping and limiting preserve the precise unsigned-low-word reads.
  Projection deliberately samples a signed word, matching `cosMul`'s argument;
  climb samples an unsigned word. Native storage has not been narrowed to 16 bits.
* Overflowing fixed acceleration differences and braking results are rejected:
  those native signed overflows had no defined C++ result. This is not a claim
  of parity for undefined inputs. Lift's old scalar `abs(speed) + 1` extreme-value
  behavior is now handled by the lift checkpoint below.
* Modern acceleration/braking retain double precision. The modern speed limit
  clamps to [0, 45000] instead of low-word wrapping/resetting; this is a deliberate
  modern policy, not fixed parity. Engine velocity units remain 27 per indicated
  knot. No new SI assumption or file-layout conversion is introduced.
* Production helpers `accelerateFlightSpeed` and `brakeFlightSpeed` preserve the
  existing order around lift computation. Horizontal speed is sampled before
  attitude updates as before; vertical speed is sampled at the vertical update.
* `typed_airspeed_tests` freezes expressions from `08b8c7b`: all 65,536 word
  values in three native-width bands, five frequencies, four difficulty braking
  rates, all headings at signed-speed boundaries, native-int limits, and 24,000
  persistent production acceleration/braking steps. Modern tests include 12,000
  fractional acceleration/braking steps and a typed speed-to-X/Y/altitude pipeline
  compared to independent double expressions. Compile-fail cases reject raw speed,
  unit/backend mixing, pointer extraction and unguarded conversion access.

Verification: Linux Release build and all 48 CTests pass, including 49 negative
compiler cases plus a positive control. Clang analysis of
`typed_airspeed_tests.cpp` reports no diagnostics. Its ASan/UBSan run passes with
the test, inline library and `egflight.c` instrumented, exercising the production
acceleration/braking helpers. Other linked core files are not instrumented and
this does not execute an entire sortie. Windows, Android, browser, external-asset
validation and live-flight checks remain outstanding.

## Lift and trim checkpoint

* `g_liftForce` and `g_rollPitchTrim` now store typed angles. Despite the old
  variable name, the lift formula produces an angular correction (word units),
  not a force in newtons. `AerodynamicsMath` computes lift correction and banked
  pitch trim for both backends. The correction and trim remain typed through
  flight-path subtraction and climb calculation.
* Fixed lift retains the signed stall-speed word, native signed velocity
  magnitude, quotient narrowing to a word before the unsigned 8192 clamp, and
  rounded Q15 cosine multiplication for trim. The three native velocity values
  for which `abs(v) + 1` was undefined are explicitly rejected before changing
  either live correction. Modern lift uses fractional radians, continuous
  division, and a non-wrapping [0, pi/4] clamp. Negative modern correction clamps
  to zero rather than acquiring the fixed path's unsigned-word interpretation.
* The production `updateFlightLift` remains between acceleration and braking,
  preserving the existing sampling order. Stall-speed generation, autopilot
  assist arithmetic and HUD coordinate conversion remain transitional raw
  consumers/producers with explicit adapters, not migrated subsystems.
* Camera snapshots capture and restore typed trim. `PoseInterpolation::linearOffset`
  preserves signed linear interpolation, not shortest-arc angle interpolation.
  The existing roll-discontinuity snap policy is unchanged. A fixed interpolation
  product overflow is rejected before any camera globals are written.
* Tests freeze the `8c0f982` formulas, including exhaustive signed stall words,
  unsigned velocity samples, every roll angle, signed/native boundaries and
  24,000 production lift/trim/climb cases. Modern fractional lift-to-climb checks
  cover 12,000 cases. Camera tests cover typed restore, interpolation, roll-flip
  snapping and unchanged live state on interpolation failure.

Verification: Linux Release build and all 49 CTests pass, including 54 negative
compiler cases and a positive control. Clang analysis of
`typed_aerodynamics_tests.cpp` reports no diagnostics. ASan/UBSan passes with that
test, inline math and `egflight.c` instrumented, including the production
lift/trim/climb callers. A separate ASan/UBSan run of
`egsys_internal_behavior_tests.cpp` (including `egsys.c`) also passes. Other linked
core files are not instrumented; whole-sortie and cross-platform checks remain
outstanding.

## Stall checkpoint

* `g_stallSpeed` now uses `StallSpeed<B>`, distinct from current `FlightSpeed<B>`
  and acceleration quantities. Fixed storage remains a signed word; modern
  storage is a fractional double. `liftCorrection` now requires the threshold
  type rather than accepting an arbitrary current-speed value in that role.
* `AerodynamicsMath::aboveStall` and `belowStall` preserve the fixed path's strict
  unsigned-word comparisons, including equality and native velocity values beyond
  one word. The modern path compares full fractional values without word wrap.
* `stallResponse` returns both stall status and a typed nose-drop angle. Fixed
  normal/severe drops keep the original divide-by-four/divide-by-two per-tick
  rounding, independent of tick frequency. Even a deficit that rounds to zero
  remains a stall response. Modern drops retain fractions and multiply the rate
  calibrated at the current 15 Hz simulation by the supplied elapsed time. This
  timing policy is deliberate, not an assertion of fixed parity at other rates.
* `correctFlightStall` is the production pitch-correction caller. It preserves
  ground suppression, difficulty/damage severity selection and the dirty flag.
  The warning sound remains at the original call site after pitch correction.
  Corner-speed generation and conversion to a stall threshold remain a raw
  producer pending migration of the thrust/load-factor calculation.
* `typed_stall_tests` freezes the `02e55d1` expressions: all 65,536 threshold words
  with twelve signed/native velocity cases, three frequencies and both severity
  levels; corner-speed conversion; and 24,000 production pitch/dirty-state cases.
  Modern tests cover fractional thresholds, no word wrapping, and equal elapsed
  time at 15/30/60/120 Hz. Invalid enums, non-finite inputs and overflowing deficits
  are rejected. Compiler-negative cases cover threshold roles, backends, raw
  construction/extraction, pointers, time and severity.

Verification: Linux Release build and all 50 CTests pass, including 61 negative
compiler cases with a positive control. Clang analysis of `typed_stall_tests.cpp`
reports no diagnostics. ASan/UBSan passes with that test, inline math and
`egflight.c` instrumented, including the real stall caller. Other linked core
files are not instrumented; whole-sortie and cross-platform verification remain
outstanding.

### Next acceptance boundary

Aircraft Euler, command, altitude, climb, airspeed and fine horizontal position
storage plus stall threshold, lift correction and pitch trim are typed, but most scalar control
producers and consumers remain legacy math. Migrate target-speed generation,
corner-speed and thrust/force generation, object state and remaining
read adapters before selecting modern flight math; changing the angle backend
alone would still quantize at these consumers. Keep modern refresh policy distinct from the original periodic rebuild,
which intentionally quantizes fixed state. Then migrate flight
position/velocity/forces and their consumers with explicit
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
| Orientation | Typed persistent matrix/Euler state, fixed/double recovery, axis deltas and render pose interpolation | Typed control inputs, legacy scalar consumer removal and modern refresh integration |
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

### Modern high-altitude follow-up

The modern HUD no longer truncates altitude modulo 65536 and generates its
thousands labels numerically. This does not remove the separate signed scene-height
boundary: `advanceFlightAltitude` still stores compressed height in `g_viewZ`,
which becomes negative at flight altitude 98304. A production characterization
test records that remaining boundary explicitly.

Modern low-altitude turbulence now takes typed flight altitude and speed directly,
avoiding this narrowed scene height. A centered-stick production flight-loop
regression at altitude 131072 failed before that change and passes afterward.
The fixed turbulence path is unchanged. Integer turbulence amplitude remains an
explicit boundary to the existing deterministic random-command generator;
1000 scene-height units and the response divisor 32768 remain gameplay tuning.

The modern ground-avoidance assist also takes typed altitude, attitude, trim,
and pilot command directly. Its centered-stick production regression covers
both assisted and unassisted difficulty at altitude 131072. Low-altitude tests
retain the original response tuning while allowing fractional modern commands.
The fixed assist expression remains unchanged.

Modern altitude-hold, recovery guidance, and propulsion now obtain their current
typed scene height directly from flight altitude. Fixed builds still read the
stored scene-height word at those call sites, preserving its update timing.
The production regression checks acceleration at altitude 131072 and verifies
that a lower autopilot target commands descent, across assisted/unassisted
difficulty. The existing 899-knot target-speed ceiling is unchanged gameplay
policy, not a newly imposed storage limit.

Other `g_viewZ` consumers still require migration, including ground-state checks
and camera state. The autopilot target itself remains a legacy word. These tests
do not establish unrestricted high-altitude flight or rendering stability.

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
