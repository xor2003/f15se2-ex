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
removed altitude/speed cutoffs, and executes 120 production flight steps,
plus `modern_sortie_tests`, which runs the shared scripted sortie and is
described in the acceptance section below. The smoke test alone is not a
completed sortie or full modern behavior coverage.

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
  own rejection tests. The same check also ratchets declarations: every raw
  scalar global (`extern` scalars in `src/**.h`, file-scope scalar definitions
  in `src/**.{c,cpp}`) must be a reviewed entry in
  `tools/sim_global_allowlist.txt`; unlisted additions fail, and entries for
  globals that were migrated to typed declarations fail as stale so the list
  shrinks as the migration proceeds. Python is required for native test
  configuration. This textual check is not an AST audit of all numerical state
  and cannot prove whole-game migration or detect every spelling/alias of a
  bypass.
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

## Indicated-knots checkpoint

* `g_knots` was `speedWord(g_velocity) / 27`: under the modern backend this
  truncated the fractional speed to a 16-bit word (wrapping above 65,536 engine
  units) and then to whole knots. `AirspeedMath::indicatedKnots` now produces a
  `CornerSpeed<B>` (the existing indicated-knots unit); `flightKnots()` is the
  typed accessor — fractional and unwrapped under modern, the stored word and
  its update timing under fixed. `flightCornerSpeed()` exposes the typed corner
  speed captured where `g_cornerSpeed` is written.
* Decision consumers now read typed knots: the attitude-control gate, gear
  auto-raise and approach brake/corridor checks, turbulence floor (`egflight.c`),
  landing/crash checks (`egframe.c`), the eject-survival roll and gear indicator
  (`egkeys.c`), and the stall-text gate (`egtacmap.c`). `Angle` gained
  `isNegative`/`isPositive` so the ground pitch-leveling check reads the
  fractional sign instead of a quantized word.
* `g_knots`/`g_cornerSpeed` stayed `int16` words at this checkpoint for display,
  engine-pitch audio and blackbox telemetry; they were migrated to
  `CornerSpeed<GameBackend>` in the indicated-airspeed storage step recorded
  below. `speedFromKnots` reproduces the `g_knots * 27` reconstruction under
  fixed and keeps the fractional product under modern. `velocityUnitsPerKnot`
  is now the single shared 27-units-per-knot constant in `AirspeedMath`.
* `typed_airspeed_tests` checks the fixed conversion over every 16-bit speed
  word (plus native-width offsets), the `knots * 27` reconstruction, thresholds
  and same-unit comparisons; modern coverage keeps `speed / 27` exact through
  1e6 engine units. `modern_flight_smoke_tests` regresses the accessor against
  `g_velocity` above the word wrap and verifies the typed values stay in sync
  with the display words across `stepFlightModel`. Eight compile-fail cases
  reject primitive/unit/backend misuse and fractional thresholds.

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
* `flightStallWarningRequired` now delegates to
  `AerodynamicsMath::stallWarning(Angle, RenderHeight)`. The fixed path preserves
  the original `signedAngle(pitch) < 0 || (uint16)height < 200` expression exactly.
  The modern path evaluates the fractional pitch sign directly and compares the
  full-range scene height, so a sub-word negative pitch still warns and an
  altitude of 229376 no longer wraps `g_viewZ` to zero and produces a spurious
  low-altitude warning. `modern_flight_smoke_tests` carries the caller
  regression; `typed_stall_tests` checks the fixed helper over every 16-bit
  height word and the modern helper at fractional pitch and heights up to 1e6.
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

## Map-position checkpoint

* `flightMapPosition()` in `egflight.c` is the typed source of the player's
  coarse map position. Fixed builds return the stored `g_viewX_`/`g_viewY_`
  words, preserving their update timing; modern builds derive fractional map
  units from typed `g_ViewX`/`g_ViewY` and do not consult the stored words.
* `MapPosition<B>` gained backend storage (signed X/Y words under fixed, doubles
  under modern), `MapMath<B>` provides map-unit conversion and interpolation,
  and `MapBoundary<B>`/`legacy::mapWordX`/`mapWordY` extract raw coordinates or
  wrap to `int16` words with a defined mod-2^16 wrap. Fixed map units preserve
  the original `(fine + 0x10) >> 5` quantization and the `0x8000 - units` Y
  mirroring; modern map units keep fractions and reject non-finite input.
* Coarse-map decision consumers now read `flightMapPosition()` and narrow only
  through the boundary words where a word-domain formula or frozen field
  requires it: tactical-map bearing and drawing plus the HUD grid origin
  (`egtacmap.c`), threat reference/range/bearing (`egthreat.c`), ownship
  projection and radar coordinate differences (`egui.c`, `egtarget.c`), target
  map writes, projectile launch coordinates and bullet hit-distance terms
  (`egcombat.c`), crash-camera map writes (`egkeys.c`), and the mission/eject
  map checks (`egframe.c`). `CamSnapshot` — new internal state, not a frozen
  layout — stores `MapPosition<GameBackend>`, interpolates via
  `MapMath::interpolate`, and narrows back to the legacy globals only through
  the boundary words.
* `typed_horizontal_tests` covers the fixed map path against the original
  expressions: X quantization, Y mirroring, word extraction, `lerpLinear`
  equivalent interpolation and equality. Modern tests cover fractional map
  units, word wrapping only at the explicit boundary, fractional interpolation
  and non-finite rejection. `modern_flight_smoke_tests` carries the caller
  regression: fractional map units derived from `g_ViewX`/`g_ViewY` while the
  stored words hold stale values. Compile-fail cases reject raw construction,
  backend mixing and coordinate extraction.

Verification: Linux Release build and all 58 CTests pass, including the new
negative compiler cases; `modern_flight_smoke_tests` passes. These tests do not
establish world-coordinate rendering stability.

## Render-boundary audit checkpoint

The remaining `g_viewX_`/`g_viewY_`/`g_viewZ`/`fineUnits` reads were audited
and split into reviewed render boundaries versus decision math:

* Decision stragglers migrated: `computeTargetBearing` (target range/bearing
  for lock and HUD) sources the player side from `flightMapPosition()` —
  exact under fixed since the targets are already coarse words.
  `computeLoftAngle` (weapon-release cue feeding `projectile.targetRef`) keeps
  the fixed `(uint16)(g_viewZ + 0x1000)` divisor but uses the unwrapped scene
  height under modern, so the cue stays meaningful above altitude 229376.
  `aircraftInsideReplacementTerrain` (`replacement_terrain_collision.h`) reads
  the backend coordinate rep directly — fractional fine position under modern —
  and the unwrapped scene height for Z, so a high modern aircraft cannot alias
  onto terrain the wrapped word would place it inside.
* Confirmed render boundaries (kept word-domain deliberately):
  `drawWorldObjectCore`/`worldPointToCamera`/`drawWorldLine` in `egmath.c`,
  `projectWorldToHud`/`projectWorldToHudFine` in `egtgt2.c`, the camera-eye
  globals (`g_camEyeX/Y/Z` + Q8 fractions) and the `drawWorldObject` calls in
  `egtarget.c`. Their outputs are int16 submit units and screen pixels — the
  word-space renderer is the reviewed boundary itself. The blackbox
  diag/snapshot `fineUnits` reads are telemetry boundaries.
* `modern_flight_smoke_tests` carries regressions: target range must equal
  the derived-position result and differ from the stale-word result; the loft
  divisor at altitude 229376 uses the unwrapped 65536 (not the wrapped 0).
  The terrain-collision path executes in the smoke's `updateFrame` calls but
  has no dedicated precision assertion without terrain assets.

Verification: fixed 58/58 and modern smoke pass after the audit edits.

## Analog input checkpoint

* `flightInputCommands(preferAnalogStick)` in `egflight.c` selects the input
  path where the byte path would have read the physical stick — same
  `g_inputDisabled` and Android autopilot-neutralization gating, same
  `input_preferGamepad()` device gate. Under modern it calls
  `joy_physicalStick()` (calibration-aware normalized SDL axes) through
  `FlightControlMath::fromAnalog`; the fixed backend and the keyboard
  virtual stick always take the byte curve.
* `legacy::analogResponseForLegacyCurve()` names the profile: endpoints match
  the byte curve's full deflection (roll ±126, pitch +42/−21 in 128-word-rate
  units — pitch's `p < 0 → ++p` halves negative authority) and axisByte's
  8000/32768 deadzone. Response is linear inside the deadzone — the nibble
  quantization is a legacy limit, not reproduced. The byte curve negates roll
  (right deflection → negative command), so the call site flips the analog
  roll sign to keep the same physical feel.
* `AnalogStick` gained `roll()`/`pitch()` accessors for that sign convention.
  `joyAxes` still update through `readCalibratedJoystick` for the tacmap
  control marker and blackbox axis recording — the byte pair remains the
  display/telemetry boundary.
* `typed_flight_control_tests` checks the profile endpoints against the
  exhaustive byte-curve oracle at ±1/0 and the deadzone edges at 0.2441 vs
  0.25. `modern_flight_smoke_tests` checks the wiring: `preferAnalogStick`
  sources the physical stick (no device → centred → zero commands) while the
  byte path still reads `joyAxes`. `joystick_behavior_tests` already covers
  `joy_physicalStick()` on a virtual device. The in-loop call on real hardware
  is compile-covered only.

Verification: fixed 58/58 and modern smoke pass.

## Whole-sortie fixed-parity checkpoint

`tests/sortie_parity_tests.cpp` drives the real flight loop
(`input_pumpEvents` → `stepFlightModel` → `updateFrame`) through a scripted
660-tick sortie — throttle ramp, gear up, climb, banked and counter turns,
throttle cut, push-over, stall pull, weapon select, missile/gun fire,
sustained turn, gear down + airbrake, descent, autopilot — and hashes five
field groups (flight/camera/objects/weapons/mission, same split and FNV mix
as `blackbox_diag`) after every tick.

* The committed golden `tests/goldens/sortie_parity.trace` was generated by
  the *same source* built at the `pre-scene-height-migration` tag (e28b9a4)
  in a scratch worktree — the oracle is the actual pre-migration binary, not
  a self-consistency check. Replay at HEAD: all 660 ticks identical.
* Inputs are deterministic end-to-end: no timer IRQ is installed
  (`timerPump` no-ops), `g_inputDisabled=1` through the mission-init frame so
  `seedRng()` takes the fixed-seed path (`g_rngSeed`) instead of
  clock-seeding — this also pins `initFrameRandom`'s `frameTick`
  randomization — then input enables. Discrete commands go through real SDL
  key events into the BIOS ring; stick bytes are written to `g_joyRawX/Y`
  after each pump (no joystick attached → `input_preferGamepad` stays on the
  keyboard curve, the same path blackbox replays axes through). A few
  `MapTarget`/`SimObject` contacts are seeded so threat/targeting/object
  paths engage; `missleSpec` ammo comes from `commData->weaponCount` so the
  fire keys reach `fireMissile`.
* Negative self-check: a one-character throttle-step change in
  `stepFlightModel` diverges the flight hash at tick 0 with a field-level
  dump (`sortie_parity_tests dump` prints the raw fields per tick).
* Honest limits: this covers the scripted profile on an empty world — no
  loaded mission assets, terrain, rendering, or real-time pacing. It does
  not cover interactive `.bbx` recorded sorties, and the exact-hash golden
  is fixed-backend only (modern acceptance lives in `modern_sortie_tests`,
  next section). A passing run proves the migration series preserved fixed
  behavior *for this profile*, not every reachable game state.

Verification: fixed 59/59 (new `sortie_parity_tests`) and modern smoke pass.

## Modern sortie acceptance gate

`tests/sortie_harness.hpp` now holds the scripted-sortie driver (seed 20967,
660 ticks, the same takeoff/climb/turn/stall/weapons/autopilot schedule),
shared by `sortie_parity_tests` (fixed, exact-hash golden) and
`modern_sortie_tests` (modern build only). The harness also emits a
per-field trace — every hashed field by name and value each tick — used as
the comparison basis (`tests/goldens/sortie_fields.trace` for fixed,
`sortie_fields_modern.trace` for modern).

`modern_sortie_tests` applies three layers:

1. **Modern golden pin** — all hashed fields, every tick, compared exactly
   against `sortie_fields_modern.trace`. The modern backend is deterministic,
   so any behavioral change breaks the pin and the golden is regenerated
   deliberately via `modern_sortie_tests record <file>`. This is the primary
   regression net for modern behavior.
2. **Player-flight envelope vs fixed** — `f.*` fields only, with tolerances
   set to ~2x the measured divergence on this sortie: fine position 8192,
   altitude/viewZ 4096 units, attitude 2048 words (wrap-safe, ~11 deg),
   speed 1500, knots 64, and small budgets for control/load/thrust/fuel.
   Chaos legitimately decorrelates the world over 660 ticks — measured
   worst case is position ~2800 units and heading ~612 words (~3.4 deg) at
   tick 659 — but the player trajectory must stay inside the envelope; a
   dead-end state like the near-pole knife-edge park would leave it.
3. **Schedule-driven discrete transitions** — gear, eject, autopilot,
   damage, gun hits, mission status and related flags must take the same
   ordered sequence of values, each transition within 64 ticks. The one
   measured shift is the 350-knot gear auto-retract firing ~20 ticks early
   under fractional speed accumulation — a legitimate threshold-timing
   difference. Missing, extra or reordered transitions fail.

Object, weapon and projectile internals are deliberately *not* compared
against fixed: they decorrelate legitimately (at ~t390 the aircraft already
differ by ~1000 altitude units and ~3 deg of pitch, so a missile launched on
the same key follows a different attitude into the ground). They are pinned
only by layer 1.

Honest limits: same scripted profile, same empty world — rendering and
interactive recordings remain uncovered here, and it does not make modern
behavior identical to fixed; it certifies that the same scripted sortie
stays bounded-equivalent and structurally intact under modern math.
Loaded-mission and combat/AI coverage arrived later via the `--combat`
profile (see the combat sortie checkpoint).

Verification: modern `modern_sortie_tests` passes (660 ticks pinned,
envelope worst case position 2838/heading 612 at tick 659), fixed 60/60
including `sortie_parity_tests`.

## Bearing storage globals typed

`g_waypointBearing` and `g_targetBearing` are now
`Angle<GameBackend>` globals instead of int16 words. Both were written as
`signedAngle(GuidanceMath::aimBearing(...))` and read back through
`angleFromWord`, so the store/load round-tripped through the word domain
for no reason. Typed storage keeps the fractional `aimBearing` result under
modern — autopilot waypoint guidance (`egflight.c`) and the target-in-view
cone tests (`egtarget.c`) now see the unrounded bearing — while the fixed
rep is the same word, so fixed behavior is unchanged bit-for-bit. The HUD
waypoint marker (`egtacmap.c`) still truncates to pixels via `signedAngle`
at the boundary.

`g_acqAimY`, `g_viewHeading`, `g_viewHeadingOffset`, `g_aamSeekerX/Y`,
`g_trk*`, `g_hitAlt`/`g_hitMapX/Y`, `g_threatRef*` and `g_wingmanX/Y` were
reviewed and deliberately left raw: they are word-domain outputs feeding
word-domain consumers (acquisition output params, the render view-matrix
pipeline, HUD pixel math) or never-written stubs, so typing them would add
adapters without preserving anything.

Verification: fixed 60/60 (sortie golden unchanged — the typed store is
bit-identical under fixed); modern smoke gained a check that
`computeTargetBearing`'s stored bearing keeps sub-word precision; the
modern sortie trace is unchanged (no bearing-dependent decision boundary
was crossed on that profile).

## Indicated-airspeed storage globals typed

`g_knots` and `g_cornerSpeed` are now `CornerSpeed<GameBackend>` globals
instead of int16 words. `g_knots` was written
`cornerKnots(indicatedKnots(g_velocity))` — typed value narrowed to the
knots word — while `flightKnots()` re-wrapped it for decision consumers.
Typed storage removes the truncate/re-wrap: fixed `flightKnots()` returns
the stored `CornerSpeed` directly (same word rep), and the modern store
keeps the fractional indicated airspeed. All consumers narrow at the
boundary via `legacy::knotsUnits`: the turbulence formula and Android debug
print (fixed-only paths), audio engine pitch, HUD speed tape and
`drawNumber`, the tacmap corner-speed bar, and the blackbox hash/snapshot
lines. `flightKnots()` keeps its backend split — modern still reads live
`g_velocity` rather than the (one-tick-stale) display store.

Verification: fixed 60/60 with the sortie golden unchanged; modern smoke
keeps the word-level knots/corner agreement check and adds a
fraction-preservation check on the `g_knots` store; the modern sortie
trace is unchanged (every consumer truncates at the boundary, so the
stored fraction is display-inert on that profile).

## Fuel storage checkpoint

`g_fuelRemaining` is now `FuelLoad<GameBackend>` instead of an `int16`
unit count. The burn stays the original integer formula — wrapped through
`legacy::fuelFromUnits` — so fixed is bit-identical and modern fuel values
remain integer (the formula has no fraction to preserve). The empty check
reads `!isPositive()` (the original `<= 0` also caught underflow before
the zero clamp), the flameout `== 0` reads `isZero()`, and `targetSpeed`
takes the typed quantity directly instead of re-wrapping through
`fuelFromUnits`. `PropulsionQuantity` gained the same-unit comparison set,
`isZero`/`isPositive` and `-=`; `PropulsionBoundary` and
`legacy::fuelUnits` gained the matching read adapter for the fuel gauge
pixels, the 2000-unit color threshold, and blackbox/hash output.
`src/egdata.c` joined the boundary allowlist: it seeds the typed global
from a legacy unit constant at declaration time.

Verification: fixed suite 60/60 (sortie golden unchanged; the
characterization test still asserts the fuel burn cadence in raw units);
modern smoke and sortie pass with the golden unchanged.

## Wreck fall checkpoint

The falling-wreck globals are typed: `g_wreckX`/`g_wreckY` merged into
`MapPosition<GameBackend> g_wreckPos`, `g_wreckAlt` became
`TerrainHeight<GameBackend>` (signed world-elevation words — the wreck can
dip below zero before the `> 0` guard stops the fall), and
`g_wreckFallVel` became `ClimbRate<GameBackend>`. Wreck creation wraps the
packed `SimObject` seed words through `legacy::mapPosition` /
`terrainFromUnits` / `climbFromUnits`; `applyGravityFall` integrates via a
new `AltitudeMath::integrate(TerrainHeight, ClimbRate)` overload (fixed
keeps the `int16 += int16` wrap; modern keeps the fractional sum), and the
HUD/drawWorldObject and snapshot paths narrow through `mapWordX/Y` and
`terrainUnits`. `VerticalQuantity` gained comparisons, `isPositive`, and
`-=`; `AltitudeBoundary`/`legacy` gained the terrain and climb-from-units
adapters.

Verification: fixed focused tests pass (`gameplay_behavior_tests` still
asserts the raw-unit fall cadence and the terminal-velocity step;
`egsys_internal_behavior_tests` asserts the snapshot/lerp words); the
allowlist ratchet shrank by the four migrated names; modern smoke and
sortie pass with the golden unchanged.

## Ground altitude checkpoint

`g_groundAltitude` is now `TerrainHeight<GameBackend>` — the same signed
world-elevation domain the `Altitudes::ground` adapters already produced.
The `Altitudes::ground(g_groundAltitude)` wrap sites now pass the typed
value directly, `missionAtHeight` takes `TerrainHeight`, the `!= 0`
carrier/ground checks read `!isZero()`, and the remaining word consumers
(the `prevAlt` word compare, `drawAircraftShadow`) narrow through
`terrainUnits`; tests seed via `terrainFromUnits`.

Verification: fixed focused tests pass (typed altitude/airspeed/stall/
aerodynamics suites seed and compare through the adapters; sortie parity
unchanged); modern smoke and sortie pass with the golden unchanged; the
allowlist dropped the migrated name.

## Threat reference checkpoint

The threat-reference snapshot globals are typed: `g_threatRefX`/
`g_threatRefY` merged into `MapPosition<GameBackend> g_threatRefPos`,
`g_threatRefZ` became `RenderHeight<GameBackend>` (it stored
`renderWord(flightSceneHeight())`; the typed store keeps the fractional
scene height under modern and narrows only where the `WordRep` consumer
reads it), and `g_threatRefHead` became an `Angle<GameBackend>` copy of
`g_ourHead` — it is write-only state in production (never read), kept
typed for the stored snapshot. Map-event seeds wrap through
`legacy::mapPosition`; word consumers (`tgtX`/`tgtY`/`tgtZ` locals, the
alert-range deltas, hit-position copies) narrow through `mapWordX/Y` and
`Altitudes::render`.

Verification: `gameplay_behavior_tests` still asserts the seeded words
through the adapters; sortie parity unchanged; modern smoke/sortie pass;
the allowlist dropped the four migrated names.

## Hit-marker storage checkpoint

`g_hitMapX`/`g_hitMapY` merged into `MapPosition<GameBackend>
g_hitMapPos` and `g_hitAlt` became `TerrainHeight<GameBackend>`. The hit
marker is a display record: writers seed from projectile/object packed
words or `flightMapPosition()`; consumers (`findWaypointEntry`,
`mapRangeDelta` deltas, `projectWorldToHud`, `drawWorldLine`, particle
copies, the threat-reference seed) narrow through `mapWordX/Y` and
`terrainUnits`. One writer stored a compressed scene-height word
(`renderWord(flightSceneHeight())`) — that narrowing is kept explicit via
`terrainFromUnits(renderWord(...))` since the field's consumers are
word-domain display paths. Chained particle/hit assignments were split
into typed stores plus explicit narrowing.

Verification: fixed focused tests and sortie parity pass unchanged;
modern smoke/sortie pass with the golden unchanged; the allowlist dropped
the three migrated names.

## Simulation-clock and free-running angle checkpoint

`g_spinAngle` is now `Angle<GameBackend>` — a free-running display angle
advanced by `steps * perTick(0x3000)` word deltas and narrowed to the
`g_objTransform` render word at each use. The frame-pacer globals
(`g_frameRateAccum`, `g_frameTimingAccum`, `g_jiffiesPerFrame`,
`g_simStepsThisFrame`) stay plain counters: they are event counts and
derived ratios, not quantities with units — the migration rule keeps
counts and indices as ordinary integers. The deterministic-contract
summary: `frameTick`/`g_missionTick` are `Ticks`/`TickDuration`, the sim
RNG is the `g_rngSeed`-seeded `gameRand15` stream (blackbox-checked), the
render-only stream is `renderRand15`/`renderRandomRange` (never recorded
or replayed — see the render/sim RNG section), and the debrief module has
its own `randSeed`/`randState` stream. `eg3dmap.c` joined the boundary
allowlist for the spin-angle narrowing.

Verification: fixed focused tests pass (eg3drast suites seed the typed
spin angle through `angleFromWord`); sortie parity unchanged; modern
smoke/sortie pass; the allowlist dropped the migrated name.

## RNG contract audit

Full sweep of the three RNG streams confirms the deterministic contract:

* **Sim stream** (`gameRand15`/`gameRand`/`randMul`/`randomRange`, seeded
  by `g_rngSeed`, blackbox-recorded/replayed): every caller is sim-tick
  or init-path — object/particle spawns gated by `frameTick.phase`,
  damage/mask rolls, AI target selection, turbulence, gun dispersion,
  director picks. No render file (`eg3d*`, `egsphere`, `eghudr`,
  `r3d_gl`, `egtacmap`) consumes it.
* **Render stream** (`renderRand15`/`renderRandomRange`, private LCG
  state): only the `drawWorldEffects` hit-spark scatter — never recorded,
  replayed, or seeded from the sim clock.
* **Debrief stream** (`randSeed`/`randState`): confined to
  `endata.c`/`enfile.c`/`enrand.h` — end-of-mission module only.

The audit found no remaining render-path consumption of the sim stream;
the earlier `drawWorldEffects` fix was the only violation.

## SimObject.flags semantics checkpoint

`SimObject.flags` gained a named `SimObjectFlag` enum in `struct.h` with
the full bit decode. The word persists from the on-disk `FlightUnit`
record (`worldImportToEgame` seeds it from `targetFlags` and writes it
back), so the high-byte bits are overloaded: aircraft spawns use the sim
role bits (TRACKED_SITE, INTERCEPTOR, CLIMBOUT, ENEMY_AIR, EGRESSING,
ONSCREEN, ALERTED) while imported ground objects carry `targetFlags`
meanings (airbase/large/waypoint/disabled) at the same positions. The low
byte is pure dynamic state (ACTIVE, ALIVE, ENGAGED, UNDER_FIRE, PAINTED,
DESTROYED) plus the disk placement attrs LONG_RANGE/WAYPOINTED;
`SIMOBJ_PERSIST_MASK` names the `&= 0x1c1` threat-recycle clear.

All byte accesses (`flags.b[0]`/`b[1]`) were normalized to word ops with
the named constants — identical on the little-endian union — across
egcombat, egthreat, egtarget, egframe, egsys, egui, and stgen. Pure
rename: fixed suite passes bit-identical; no behavior change on either
backend.

## Threat-retarget bearing checkpoint

The AI retarget scan's two `computeBearing` calls (egthreat.c) now go
through `TrackMath::aimBearing` narrowed via `signedAngle`. Fixed is
bit-identical (`aimBearing` runs the same `computeBearing` on the
narrowed ints); modern evaluates `atan2` on the *un-narrowed* map deltas —
the original truncated the deltas to int16 before solving, so far-target
bearings wrapped. The `abs(viewBearing - candBearing)` candidate compare
keeps its original non-wrapping int semantics on both backends (a game
rule quirk, not a width limit).

Verification: sortie parity bit-identical; modern golden unchanged (the
sortie's deltas never exceeded the word, so the correction only fires on
wide maps); modern smoke passes.

## Throttle-command storage checkpoint

`g_setThrust` is now `EngineThrust<GameBackend>` — the same engine-command
domain as the typed `g_thrust` it spools toward, so the requested/actual
pair can no longer mix with plain ints. All writes go through
`legacy::thrustFromUnits` (the command is integer-unit by design — lever
positions are 0..144); unit arithmetic (`thrust²` fuel burn, `*80` brakes
approximation, `/3` gauge pixels, `clampRange` steps) narrows through
`thrustUnits`, and the recovery writer assigns `recoveryThrust()`'s typed
result directly. `EngineThrust` gained the same-unit ordering operators.

Verification: fixed focused tests pass (the characterization suite still
asserts throttle recovery/damage limits in raw units); sortie parity
unchanged; modern smoke/sortie pass; the allowlist dropped the name.

## Track-camera orientation checkpoint

`g_trkBearing`/`g_trkPitch`/`g_trkRoll` are now `Angle<GameBackend>` — the
tracking camera's smoothed view orientation. The smoother's word-domain
delta math (`(delta >> 5) * scale`) narrows the stored angles through
`legacy::signedAngle`, new tracking solutions wrap words via
`angleFromWord`, the external-view copy takes the typed `g_ourHead`/
`g_ourRoll` directly, and the offscreen `R3DScene` construction narrows to
the render word interface. Pitch comparisons against fixed thresholds are
typed. `g_trkRange`/`g_trkSize`/`g_trkScale` stay raw as scale-factor
bookkeeping with no distinct unit.

Verification: fixed suite 60/60 incl. sortie parity; modern smoke/sortie
pass; the allowlist dropped the three names.

## Target lead-angle dial checkpoint

`g_targetLeadAngle` is now `Angle<GameBackend>` — the rotating gun-lead
dial fed by the per-tick north/south sign. The `& 0xfff` wrap is kept as
an explicit word boundary (`angleFromWord` of the masked sum): the dial is
a 4096-position indicator by design, so both backends retain the same
coarse quantization. Blackbox hash and snapshot reads narrow through
`signedAngle`.

Verification: fixed focused tests pass; sortie parity unchanged; modern
smoke/sortie pass; the allowlist dropped the name.

## View-orientation storage checkpoint

`g_viewHeading`/`g_viewPitch`/`g_viewRoll` are now `Angle<GameBackend>` —
the selected camera orientation feeding the external-view eye solve and
the render matrix. `computeTrackingCameraAngles` out-params became typed
`Angle*`, so the internal `wideBearing` solution keeps its fraction under
modern instead of narrowing at the store; the direct-attitude copies take
`g_ourHead`/`g_ourPitch`/`g_ourRoll` as typed angles; the packed
`ViewSnapshot` ring and `lerpViewAngle` Q12 tween stay word-domain
(reviewed packed/interpolation boundaries) and wrap via `angleFromWord`.
The camera-eye trig calls (`sineOffsetQ8`/`cosineVelocity`) now receive
the typed angle — a real modern precision gain on the sub-pixel camera
path, same class as the view-fraction shake fixes. The gimbal-wrap
normalization and `buildRotationMatrixFar`/`render3DView` calls narrow
through `signedAngle` at the render word interface.

Verification: fixed suite passes bit-identical (sortie golden unchanged —
every write was already word-derived under fixed); modern smoke/sortie
pass with the golden unchanged (the new fraction only shifts sub-pixel
camera frac, invisible to sim state). The allowlist dropped three names.

## Angle magnitude read adapters

`legacy_rotation.hpp` gained two fraction-preserving magnitude reads for
decision sites that used `abs(signedAngle(x))`:

* `angleMagnitude(angle)` — |angle| in word units (0..32768). Fixed reproduces
  the hosted `abs(int16)` result including `abs(-32768) == 32768`; modern
  returns `|radians| · 32768/π` as a continuous value, so a roll of 0x2FFF.9
  words compares inside a `< 0x3000` gate instead of rounding out of it.
* `angleMagnitudeCompat(angle)` — the `abs16Compat` variant for sites that
  used the DOS quirk abs (`word 0x8000` stays negative). The post-abs
  `(int16)` cast sites (`(int16)abs(...)` re-wraps 32768 identically) use it
  too; the equivalence is asserted exhaustively in `typed_rotation_tests`.
* Angle differences express naturally as `Angle - Angle`: both backends give
  the wrapped short-arc difference (`Angle16` subtracts mod 2^16; modern
  normalizes to [−π, π)), so `angleMagnitude(a - b)` replaces
  `abs16Compat((int16)(wordA - signedAngle(b)))`.

Migrated decision sites: fire-missile roll gate and acquisition bearing diffs
(`egcombat`), ground pitch-down check, ground-avoidance and eject roll gates,
nonzero-roll ground leveling, high-gee flag, evasion roll threshold
(`egflight`), crash-dive heading check (`egframe`), hard-landing damage roll
(`egkeys`), loft-marker roll gate (`egtarget`), `computeLoftAngle` pitch
magnitude (`egtgt2`), threat-aspect roll gate (`egthreat`).

Deliberately unchanged: `signedAngle` remains the correct adapter where the
consumer is word-domain — `sinMul`/`cosMul` LUT inputs, `pitchCommand`/`hdg`
word producers, `Projectile`/snapshot/`heading.w` storage writes, and all
display/projection reads. The `abs(abs(wordDiff >> 8) - 0x40)` compound in the
SAM acquisition scan (`egcombat.c` ~line 203) keeps `signedAngle` — its `>>8`
is word-domain arithmetic inside the deferred combat/AI scope.

Verification: fixed 59/59 and modern smoke pass; `typed_rotation_tests`
checks the fixed adapters against the literal `abs`/`abs16Compat` oracles
over all 65536 words plus sampled wrapped diffs, and the modern fraction
preservation at a sub-word gate boundary.

## Map offset and range read adapters

Decision sites that read `mapWordX/Y(flightMapPosition())` minus a stored map
word now compute the delta on the typed position, so the modern backend no
longer rounds the player position to a coarse word before comparing:

* `MapOffset<B>` (`map_position.hpp`) — the word-unit difference: `int` under
  fixed (the original int16-promoted subtraction, unwrapped) and `double`
  under modern (fraction preserved). `ringX()/ringY()` reproduce the
  `(uint16)delta` side-tests, wrapping the *fractional* modern value onto
  [0, 65536) instead of truncating first.
* `legacy::mapOffset(pos, x, y)` — builds the offset from stored map words
  with each field's original promotion (uint16 zero-extends, int16
  sign-extends).
* `legacy::mapRange(offset)` — the `rangeApprox` approximation (max + min/2)
  on the offset. Fixed reproduces it end-to-end including the abs16Compat
  (int16) wrap, the 0x7fff cap and the int16 return cast (the delta-32768
  quirk path yields +16384, verified against the real `rangeApprox`). Modern
  wraps deltas onto the same ring, keeps the fraction, and drops the cap and
  quirk — a legacy int16-return limit, not a gameplay bound.

Migrated: snapshot-ring index (`egflight`), autopilot initial heading pick
(`egflight`), `g_northSouthSign` side test (`egframe`), closest-threat scan
(`egframe`), threat timeout range gate (`egthreat`).

Deliberately unchanged: `computeBearing` callers stay on word deltas —
bearing is an inverse-trig output whose endpoint contract is still open.
Word-vs-word `rangeApprox` calls (hit map, sim-object pairs, `g_threatRef`
locals) and `mapWordX/Y` storage/display/projection writes stay — no typed
fraction exists on the other side.

Verification: fixed 59/59 and modern smoke pass; `typed_horizontal_tests`
checks the offset/ring/range adapters against the literal promotion and the
real `rangeApprox` over ~350k sampled cases including the quirk delta.

## Projectile guidance and state checkpoint

`Projectile`'s attitude and fine position are typed: `head`/`pitch`/`bank` are
`Angle<GameBackend>` (the old `worldX/Y/Z` words) and `fineX`/`fineY` are
`FineCoord<GameBackend>` — an int32 fixed / double modern rep on the 21-bit
object ring, wrapping in the constructor exactly where the original masked
`& 0x1FFFFF`. `mapX`/`mapY`, `alt`, `speed`, `ttl`, IDs and refs stay raw
storage/protocol fields; `mapWord()` derives the coarse word only at write
time.

New math surface (`guidance.hpp`, `map_position.hpp`, `legacy_rotation.hpp`):

* `GuidanceMath::aimBearing(dx, dy)` — `computeBearing` endpoint; fixed runs
  the original LUT approximation, modern `atan2` directly.
* `limitTurn`/`turnStep`/`bankFromTurn`/`scaledStep` — the steering chain's
  `clampRange` (including the <= -0x4000 wrap-to-max quirk and int16 limit
  truncation under fixed), `(delta<<2)/scaling` step, `delta<<1` bank and
  `magnitude/scaling` gravity/dive decrements. Modern keeps the fractions the
  word shifts and truncating divides discarded, and does not wrap the clamp
  limits to int16.
* `sineStep`/`cosineStep` — `(trig(heading)*step)>>15` fine-unit advance;
  fixed is the LUT Q15 product, modern is continuous trig on the fractional
  heading, so sub-fine-unit motion survives instead of re-quantizing.
* `FineCoord::interpolate` / `PoseInterpolation::angle` — the snapshot lerp
  (`lerpLinear`/`lerpAngle` semantics verbatim, snap on the seam).
* `legacy::angleSeparation(a, b)` — the *unwrapped* `abs(word - word)`
  acquisition compare, distinct from the shortest-arc `angleMagnitude(a - b)`.
* `Angle` ordering/`sign`/`isPositive` — signed-domain compares matching the
  int16 word tests the originals used.
* `legacy::fineRep`/`fineWord` — fine-unit rep access at the seed/store
  boundaries (`g_projInterpX/Y`, snapshots, hash/dump).

Consumers: `updateThreatTargeting` steering (with `aimIsHeading` preserving the
speed-up tick's "aim at own heading" no-op — `angleFromWord(signedAngle(head))`
would pull modern toward the word grid), `samCanAcquireTarget` cone checks,
`fireMissile`/`fireAirThreat`/`fireGroundThreat` seeding, the `egsys` capture/
interpolate/restore path (TTL gating and the alt low-bit track flag kept),
`egflight` director-camera reads, `egtarget`/`egui` render-boundary reads
(`signedAngle` conversions), and the blackbox hash/dump/snapshot writers which
hash the word views so the fixed golden stays bit-identical.

Word-domain stays: `step`/`alt`/`speed` arithmetic and the `cosMul`/`sinMul`
vertical decomposition — those fields are int16/int32 protocol state shared
with snapshots, so the pitch word read there is a reviewed boundary.

Verification: fixed 59/59 — `sortie_parity_tests` replays the `e28b9a4`
golden across live projectile flight — and modern smoke pass.
`typed_guidance_tests` checks the new helpers against the real `computeBearing`,
`clampRange`, `sine`/`cosine` word oracles over the full 16-bit heading space
plus modern fractional cases.

## Bullet-track checkpoint

`BulletTrack` (the 20-entry gun-round/spark table) is typed the same way:
`posX`/`posY` are `FineCoord` on the shared 21-bit ring (same mask as
`BULLET_FINE_MASK`) and `alt`/`velX`/`velY`/`velZ` are `StepRep` — int32 under
fixed, double under modern, so the per-step velocity and the altitude
accumulator keep their fractions instead of re-quantizing each sim step.

* `GuidanceMath::sineVelocity`/`cosineVelocity` — the `sinMul`/`cosMul`
  spawn decompose: fixed runs `fixedMulQ14`'s rounded Q15 product with the
  int16 parameter and result narrowing `sinMul` applied; modern keeps
  `sin(rad)·mag` and the fractional `horizMag`/`horizVel` that the original
  stored back into an int16 between the pitch and yaw decomposes.
* `FineCoord::deltaFrom` — the signed centered ring delta (`fineWrapDelta`).
* `FineCoord::isZero` — the `posX == 0` free-slot convention.
* `GuidanceMath::fineTravel` — the `(vel * alphaQ12) >> 12` render/swept-path
  step; fixed multiplies in int32 then truncates, modern divides exactly.

Consumers: `updateBulletsAndFire` integration/spawn (player gun), the
egthreat threat-gun spawn, `drawWorldEffects` render interpolation (`fineWord`
at the project/draw calls), `roundToTargetDist2` swept-path hit test — its
`long` intermediates become `double` under modern so the segment projection
keeps fractional positions — and the hash/dump/snapshot writers on word views.

Verification: fixed 59/59 (sortie parity fires guns during the golden run)
and modern smoke pass; `typed_guidance_tests` checks the helpers against the
real `sinMul`/`cosMul` oracles plus modern fractional cases.

## Target/waypoint bearing checkpoint

The two bearing producers that compare the player position against coarse
map words now compute the delta on `MapPosition` via `mapOffset()` instead of
rounding the player to a map word first:

* `computeTargetBearing` (egtgt2.c) — `g_targetBearing`/`g_targetRange` feed
  target-lock range gates and bearing cones in egtarget.c.
* `renderHudFrame` (egtacmap.c) — `g_waypointBearing` feeds the autopilot
  guidance input in egflight.c as well as the HUD marker.

`GuidanceMath::aimBearing` widened to `double` deltas: fixed narrows to the
`computeBearing` int endpoint (integer callers exact), modern runs `atan2` on
the fractional deltas directly. Under fixed `mapOffset` sign-extends the int16
map rep identically to the original `mapWordX/Y` subtraction, and `mapRange`
mirrors `rangeApprox` literally, so both sites are bit-exact. Under modern the
fractional player position survives into the bearing/range decisions.

The SimObject AI/threat decision surface follows the same pattern. The struct
is file-layout frozen (`FLIGHTUNIT_SIZE`, memcpy'd world records), so fields
stay raw words and decision reads go through typed adapters:

* `computeThreatRangeBearing` (egthreat.c) — the player-to-threat delta runs
  on `mapOffset`/`mapRange`/`aimBearing` like `computeTargetBearing`.
* `updateObjects` (egthreat.c) — steering `bearing`/`pitchCmd` via
  `aimBearing`; `heading.w`/`pitch`/`bank.w` field reads via `angleFromWord`;
  `abs(a - b)` cone checks via `angleSeparation` (unwrapped plain diff,
  matching `abs(int16 - int16)`); the bank-steering clamp via `limitTurn`;
  `sinMul`/`cosMul` products via `sineVelocity`/`cosineVelocity`. Fields
  (`worldX`, `alt`, `heading.w`) stay word-precision because the file layout
  cannot hold fractions — the typed reads keep the math backend-explicit.
* `samCanAcquireTarget`, the SAM launch cone check, and the hit-map
  `rangeApprox` sites (egcombat.c, egtarget.c) — `mapRangeDelta`/
  `aimBearing`/`angleSeparation` on the word deltas.

`legacy::mapRangeDelta(dx, dy)` wraps `mapRange` for plain word-domain delta
pairs (no typed position to preserve); fixed reproduces `rangeApprox` exactly
including the int16 re-wrap. Maneuver-table index arithmetic (`relBearing`,
`aspect` octants) and field self-updates (`heading.w += bank.w >> 3 …`,
`posX = worldX >> 5` derives) stay raw — table addressing and boundary writes
are integer/file-layout concerns per the coverage notes.

Verification: fixed 59/59 (sortie parity exercises target selection and the
autopilot HUD path; object AI runs in the golden sortie) and modern smoke
pass; `typed_guidance_tests` adds the fractional-delta `aimBearing` case.

## Camera fine-machinery checkpoint

The tracked/tracking camera math that differences FINE world deltas is typed:

* `GuidanceMath::wideBearing` — the `computeBearing32` endpoint: fixed runs
  the shared shift-to-fit + LUT bearing on int32-narrowed deltas (integer
  callers exact); modern evaluates atan2 on the un-narrowed, possibly
  fractional deltas — no shift, no LUT.
* `GuidanceMath::wideRange` — `rangeApprox32`: max + min/2 on the magnitudes,
  int32 under fixed / double under modern, uncapped both ways.
* `GuidanceMath::sineOffsetQ8`/`cosineOffsetQ8` — `sinMulQ8`/`cosMulQ8`:
  `(sine * mag) >> 7` on the Q15 table value under fixed; `trig(rad) * mag *
  2^8` under modern.
* `eyeFromQ8` (egflight.c) takes the Q8 sum as double: the integer part is
  `floor(q8 / 256)` and the frac byte the sub-unit remainder — identical to
  `>>8`/`&0xff` on integer inputs, and under modern the fractional view
  position (`fineRep`) and trig product flow into the Q8 sum so the frac byte
  carries real sub-word precision.

Consumers: `drawTargetView` (egmath.c) — its `dxFine`/`dyFine` are now
fraction-capable (`fineRep` instead of `fineUnits`) so the tracked-model
bearing/pitch keep the modern sub-word position; `computeTrackingCameraAngles`
(egflight.c, signature widened to double) and the chase/target/follow/side
camera eye offsets in `renderFrame`, where `g_ourHead` stays typed through the
trig instead of round-tripping through `signedAngle`. The camera outputs stay
word/int32 globals — the render boundary — and the deliberate float offset
block in `drawTargetView` (keyed to the view matrix's sine table) is
untouched.

Verification: fixed 59/59 (sortie parity exercises external/tracking views
during the golden run) and modern smoke pass; `typed_guidance_tests` checks
`wideBearing`/`wideRange` against the real `computeBearing32`/`rangeApprox32`
oracles and the Q8 helpers against `sinMulQ8`/`cosMulQ8` plus modern
fractional cases.

## SimObject fine-position shadow checkpoint

The packed `SimObject.worldX/worldY` int32 fields are the frozen FlightUnit
file layout — they cannot widen in place. They now carry a typed shadow:
`g_simObjectFineX/Y` are `ViewCoordinate<B>` arrays whose rep is int32 under
fixed and double under modern. Every position write flows through
`legacy::objectFineSet`/`objectFineAdvance`, which updates the shadow AND the
packed cache; every fraction-capable consumer reads `objectFineRep`:

* spawn/load paths (worldxfer memcpy, egcombat/egframe spawn sites) seed the
  shadow from the same rep the packed field gets;
* the AI move update (egthreat) advances the shadow by the StepRep velocity,
  so modern sub-fine-unit motion accumulates instead of truncating at the
  int32 store each tick;
* projectile/threat-gun launches seed `FineCoord` from `objectFineRep` —
  the fractional launcher position flows into the new projectile under modern;
* the render-snapshot system (`SimObjSnap`, egsys.c) stores the rep itself,
  so interpolation keeps sub-fine precision under modern while fixed reduces
  to the original `lerpLinear` int32 math.

The packed `worldX/Y` stay correct int32 caches: serialization, the teleport
guard, `posX = worldX >> 5` derives, camera targets and HUD projection read
them unchanged. `planes[]` stays word-only — its entries are word-authoritative
targets, not integrated positions.

Verification: fixed 59/59 (sortie parity exercises the object AI loop),
modern smoke, `typed_horizontal_tests` covers fixed equivalence (seed,
advance, negative step, int32 wrap) and modern fraction accumulation plus
packed-field truncation.

## SimObject attitude shadow checkpoint

The packed `SimObject.heading/pitch/bank` int16 words are the same frozen
FlightUnit layout — they cannot widen in place. They now carry typed shadows:
`g_simObjectHeading/Pitch/Bank` are `Angle<B>` arrays whose rep is the Angle16
word under fixed and radians under modern. Every attitude write flows through
`legacy::objectAttitudeSet`/`objectAttitudeAdvance`, which updates the shadow
AND the packed cache; fraction-capable decision reads use the shadow directly.

* the AI maneuver core (egthreat `updateObjects`) integrates attitude on the
  shadow: `bank += (rollCmd * missionFactor) / scaling` becomes
  `objectAttitudeAdvance(..., attitudeStep(rollCmd * f, rate))`, `heading +=
  (bank >> 3) / scaling` becomes `bank.shiftedDown(3).dividedBy(rate)` — under
  modern the sub-word quotient accumulates instead of truncating each tick;
* `Angle::shiftedDown`/`dividedBy` reproduce the int16 arithmetic shift and
  truncating divide exactly under fixed; `wordRep`/`uwordRep` expose the
  signed/unsigned word-domain rep for control-signal diffs and the
  `(uint16)`-cast products the originals spelled raw;
* `wordClamp` ports `clampRange` to word scalars including the
  `value <= -0x4000 selects max` wrapped-angle quirk; `attitudeStep` is the
  `words / divisor` angle step (fixed int divide, modern fractional);
* the pitch>0x4000 pose flip is typed: `head/bank += halfTurn()`,
  `pitch = halfTurn() - pitch` — identical mod-2^16 to the original byte
  writes (`b[1] += 0x80`) and `0x8000 - pitch`;
* `moveAmt`'s `(uint16)(-(pitch/2 + 0x8000)) * speed >> 14` becomes
  `wordProductQ14(uwordRep(-(pitch.dividedBy(2) + halfTurn())), speed)`;
* spawn/load/snapshot paths seed or restore the shadow: `spawnEnemyAircraft`,
  egframe spawn seeds, worldxfer's post-memcpy loop, and `SimObjSnap` now
  stores `Angle` head/pitch/bank so `Pose::interpolate` (same snap thresholds
  and shortest-arc blend as the removed `lerpPose`) tweens fractional
  attitude under modern and restores it without re-quantizing;
* `pitchDelta` is `WordScalar` (int16 fixed / double modern): the raw
  `pitchCmd - pitch` difference deliberately does NOT go through
  `Angle::operator-`, which arc-wraps — bounded analysis shows the raw diff
  never reaches the wrap-divergence band for these inputs;
* packed-word consumers stay packed mirrors: egtarget/egui render reads,
  `aspect`/`relBearing` bucket math, `worldSamTable` serialization.

Verification: fixed 60/60 (sortie parity exercises the AI attitude loop),
modern smoke, `typed_rotation_tests::attitudeShadow` covers fixed oracles for
every new op (shiftedDown/dividedBy/wordRep/uwordRep/wordClamp/attitudeStep/
wordProductQ14) plus the set/advance sync contract and modern fraction
accumulation.

## Simulation clock typed checkpoint

The int16 sim clock `frameTick` and the deadline globals
(`g_destroyedCueDeadline`, `g_directorEventDeadline`) are now `Ticks`
(`src/math/ticks.hpp`) — the first application of the class-based-domain
directive: no implicit conversion to or from any primitive, so every
remaining raw use is a compile error rather than silent word arithmetic.

* `Ticks` is deliberately not backend-templated: the clock is an int16 word
  under both backends and its wrap/phase semantics are gameplay behavior.
  Named operations replace the magic masks and shifts at every call site:
  `phase(period)` for `v & (period-1)` cadences, `bit(n)` for single-bit
  tests, `ring(shift, count)`/`uring(shift, count)` for rotating slot
  selectors, `shifted`/`mod`/`umod` for the signed/unsigned arithmetic the
  originals used, `offset(n)` for int16-wrapping deadline arithmetic, `++`
  with defined int16 wrap, `isZero()` and `word()`/`uword()`/`fromWord()`
  as the only raw boundary.
* All `frameTick` consumers migrated: egframe (director/destroyed-cue
  deadlines, bullet/particle slot selectors, RNG reseed), egthreat (AI
  phase selectors, roll-command bit test — `(frameTick >> 8) & 8` is
  `bit(11)`), egcombat (speed throttles, indicator blink, visibility
  phases, destroyed-cue deadline), egflight (eject smoke index, fuel
  cadence `umod`, view-snapshot ring indices), egtarget/egtacmap/egkeys
  (display blink bits), blackbox diagnostics/snapshot (`word()` at the
  serialization boundary) and egmain (sentinel arm).
* Tests seed/compare through `Ticks::fromWord(...)`/`word()`; the sortie
  hash keeps reading `g_directorEventDeadline.word()` so the golden
  comparison is unchanged.
* `typed_ticks_tests` covers every named op against literal int16 oracles
  (wrap at ±32767, signed vs unsigned shift/mod, `(v>>8)&8 == bit(11)`,
  deadline sentinel/arm/fire pattern); five new compile-fail cases prove
  `Ticks` rejects primitive construction, conversion, assignment and
  mixed arithmetic.

Verification: fixed 60/60, modern smoke, analyzer clean on all touched
units. The int16 wrap itself is preserved under modern — tick arithmetic
is identical on both backends by design; what modern keeps fractional is
the state advanced per tick, not the tick count.

## Tick-duration domain checkpoint

The countdown/elapsed timer globals are now `TickDuration` (same header):
`g_eventTimers[]`, `g_threatActiveTimer`, `g_scopeSweepTimer`,
`g_landingTimer`, `g_joyCalibTimer`, `g_hudMsgTimer`, `g_dirMsgTimer`,
`g_missionTick`, `g_hitEffectTimer`, `g_threatTimerInit` and
`g_lastSpawnTick`. `Ticks` (instant: phase/ring/deadline arithmetic) and
`TickDuration` (count: decrement, expire, elapsed deltas) are distinct
types — a duration has no instant-phase meaning and an instant never
counts down, so the domains cannot mix without explicit `word()` reads.

* Countdown semantics: `isZero()`/`isPositive()`/`isNegative()`/
  `atMost()`/`exceeds()`/`equals()`/`below()` threshold tests, typed
  `++`/`--` with int16 wrap, `stepTowardZero()` for the hit-effect decay
  idiom (`v -= sign(v)`).
* Elapsed semantics: `phase()`/`shifted()`/`ring()` cadence selectors on
  `g_missionTick`, `elapsedSince()` for spawn-throttle deltas,
  `elapsedWithin(total)` for `scaling - countdown` reads.
* `g_replayLog.events[].coord` (packed record) and blackbox hash/snapshot
  reads use `.word()` — the reviewed serialization boundary.
* `planes[].threatTimer` stays a packed word field (layout-locked
  `PlaneEntry` +0x0A) — a reviewed boundary, not migrated.
* Test fixtures seed/compare via `fromWord`/`word()`/`equals`/`isZero`;
  four more compile-fail cases (primitive construct/extract/assign plus
  Ticks→TickDuration mixing) guard the boundary.

Verification: fixed 60/60 (sortie parity unchanged — mission tick and
timers feed the golden hash), modern smoke, `typed_ticks_tests` covers
duration semantics against int16 oracles, analyzer clean.

## Sim-rate divisor checkpoint

`g_frameRateScaling` — the sim-ticks-per-render-frame divisor shared by
every per-tick rate — is now `SimRate` (same header). `x / scaling`,
`n * scaling` and `scaling << n` are gone from production code; the ~70
call sites read `rate.perTick(x)`, `rate.scaled(n)`, `rate.shifted(n)`,
`rate.minus(n)` or `rate.word()` at mixed-type boundaries (function
params, index math, `(int32)`/`(char)` casts).

`perTick` is deliberately generic: `v / rate` keeps the operand's own
arithmetic — integer division for integral dividends (unchanged fixed
semantics) and a real quotient for double step reps, so a fractional
modern consumer survives the divide instead of truncating early.
`g_threatDisplayTtl` and `g_savedSamTtl` joined `TickDuration` in the
same pass; `TickDuration` gained `atLeast` and same-domain threshold
compares.

Tests seed/compare via `SimRate::fromWord`/`word()`; four compile-fail
cases cover primitive construct/extract/assign and `x / rate` mixing.
`typed_ticks_tests` exercises `perTick` (both signs, sub-rate truncation,
double fraction), `scaled`/`shifted`/`minus` and threshold compares
against int16 oracles.

Verification: fixed 60/60, modern smoke, compile-fail suite extended,
analyzer clean on all touched units.

## SimObject alt/speed linear shadow checkpoint

`SimObject.alt` and `SimObject.speed` were the last packed `int16` fields
integrated every sim tick (`alt += (int16)sineVelocity(pitch, moveAmt)`,
`speed +=/-= scaling.perTick(...)`), which quantized each step to a word
under modern. They now follow the same shadow contract as attitude:

* `g_simObjectAlt[]`/`g_simObjectSpeed[]` are `WordRep<GameBackend>`
  (int16 fixed / double modern) shadows declared beside the attitude
  arrays; the packed `SimObject` words remain the synced layout/render/
  serialization mirrors.
* All writes go through `legacy::objectLinearSet`/`objectLinearAdvance`,
  which narrows each delta to the int16 word the original cast produced
  under fixed and keeps the fraction under modern. `wordLerp` ports the
  `lerpLinear` snapshot blend onto word reps.
* Decision compares read the shadow (`speed != 0`, `alt < 0`,
  `alt <= 30000`, `speed < maxSpeed`, the pitch-vs-alt dive gates);
  word-domain consumers — `tgtZ` aim words, `wordProductQ14` magnitudes,
  `testWorldPosVisible`, `g_hitAlt`/`g_wreckAlt`, bullet tracks, HUD
  diffs — read the packed mirror, the canonical rounded word view.
* `SimObjSnap.alt` carries the rep; world-file load re-seeds the shadows
  after the `FlightUnit` memcpy. Tests seed through `objectLinearSet`.

`typed_rotation_tests::linearShadow` oracles the helpers against int16
wrap/truncation and proves modern fractional accumulation and wide deltas.

Verification: fixed 60/60 (sortie parity exercises the per-tick chain),
modern smoke, analyzer clean (egthreat retains the documented pre-existing
warnings).

## Projectile alt flag-aliased shadow

`Projectile.alt` is the same per-tick-integrated packed word
(`alt += (int16)sineVelocity(pitch, speed<<7/scaling)`) with one extra
quirk: its low bit is the radar track-state flag, written with byte ops
(`*(uint8*)&alt |= 1` / `&= 0xfe`) that fold ±1 into the stored value.
`g_projectileAlt[]` is the `WordRep` shadow; `Projectile.speed` stays
packed — it only ever holds whole speeds (`speed++` ratchet, const seeds),
so there is no fraction to preserve.

* `objectLinearSet/Advance` cover the plain writes; `objectLinearFlag0`
  reproduces the flag byte-ops (packed bit0 moves the shadow by ±1, exactly
  like the original word arithmetic).
* `objectLinearInterpFlagged` ports the snapshot blend
  `((lerp & ~1) | (pn.alt & 1))`: `ProjSnap` carries the altitude rep plus
  the next-snapshot flag bit separately.
* `alt == 1`, `alt & 1`, `testWorldPosVisible`, `g_hitAlt`, and the
  `(alt0 - alt) >> n` aim deltas read the packed word — the canonical flag
  + rounded-altitude view; `alt < 0` (impact check) reads the shadow.

Verification: fixed 60/60, modern smoke, analyzer clean.

## Projectile ttl TickDuration checkpoint

`Projectile.ttl` — the per-tick flight-time countdown (`ttl--`, `== 0`
free-slot checks, lockRange-derived loads, `clamped` ceilings) — is now a
`TickDuration` field inside the runtime-only `Projectile` struct. `ttl--`,
`isZero()`, `atMost`, `exceeds(scaled(2))`, `clamped(0, shifted(4))` and
same-type copies into `g_savedSamTtl` replaced the raw int16 idioms;
`ProjSnap.ttl` carries the type and the one-step interp gate compares
`.word()` reps. `mapEvents[].ttl` stays packed — `MapEvent` is a 12-byte
file-layout record. Diagnostics read `.word()` at their int16 boundary.

Verification: fixed 60/60, modern smoke, analyzer clean.

## Spec-table unit-scale naming checkpoint

`src/spec_units.hpp` names the `sams[]`/`aircraftTypes[]` table-unit
conversions that `struct.h` field comments documented only as raw
formulas: `specProjSpeed` (maxSpeed >> 6), `specLockRangeUnits`
(lockRange << 3), `specYawClamp` (turnRate * 0x80),
`specPitchDiveLimit`/`specPitchClimbLimit` (turnRate << 11 / << 9 — the
asymmetric dive-vs-climb clamp), `specRollCmdClamp` (maneuverability *
0x1000) and `specBankStepClamp` (maneuverability * 256, the per-tick
rollCmd-vs-bank lead). The HUD lock gates get the inverse direction:
`specRangeFromDepth` (-projDepth >> 3), `specRangeFromDepthApprox` (the
A2G path's / 7) and `specFirmRangeFromDepth` (>> 2, the red-tint close
lock). All expressions are arithmetically identical —
pure naming, no behavior change. `SimObject.flags` bit masks stay hex:
the project policy classifies packed flags as bookkeeping, not math, and
per-bit semantics are not established.

Verification: fixed 60/60 incl. sortie parity (exercises the guidance
clamps and ttl estimates), modern smoke, analyzer clean on both TUs.

## Acquisition range globals checkpoint

`g_acqRange` and `g_nearestThreatRange` are now `WordRep<GameBackend>` —
they carry `mapRange`/`mapRangeDelta` results, which are fractional and
uncapped under modern (`mapRange` drops the legacy 0x7fff return cap).
The `int16`/`uint16` stores re-narrowed that: `(int16)dist` truncated the
fraction, and `(uint16)mapRangeDelta(...)` could wrap a >64k modern range
into the small-hit-compare domain. `updateThreatTargeting`'s `best`/`dist`
locals follow the rep, `abs((int16)best)` reads through the new
`wordAbs` compat helper, and the `(unsigned)` compare casts drop —
the values are non-negative by construction. `g_acqAimY` stays int16 —
it's a word-domain out-param by contract.

Verification: fixed 60/60 incl. sortie parity, modern smoke, analyzer
clean on egcombat/egframe/egdata.

## Target/acquisition range chain checkpoint

The whole range chain now carries `WordRep<GameBackend>` instead of
narrowing `mapRange`/`mapRangeDelta` results at every hop:

* `computeTargetBearing`/`computeMapTargetRange`/`computeSimObjectRange`
  return the rep (identical to int16 under fixed; keeps the fraction and
  drops the 16-bit range wrap under modern), as do `g_targetRange`,
  `g_acqRange`, `g_nearestThreatRange` and the `range`/`lockedRange`/
  `best`/`tgtZ`/`dist` locals in egtarget/egthreat/egcombat.
* `abs((int16)v)` reads go through the new `wordAbs` compat helper;
  `(uint16)mapRange(...) >> 6` bucket scales keep the shift but drop the
  word wrap (`(int)X >> 6`); word-domain sinks (`g_projDepth`,
  `buildRangeString`, the `& 0x200` flag test) take explicit `(int)`
  narrows at the boundary.
* `smokeSlot` stayed int16 — it is genuinely dual-domain (a particle
  index at one site, a range at another); the range use got its own
  `scanRange` local instead of retyping the shared slot.
* Test extern decls updated to the real signatures — the int16 return
  decls would have read the wrong register under modern.

Verification: fixed 60/60 incl. sortie parity, modern smoke (its
`g_targetRange` assert now truncates at the word boundary it means to
check), analyzer clean on egthreat/egtarget/egtgt2.

## Model-submit fraction checkpoint (external-view shake fix)

The F5–F9 model shake was a modern-only boundary loss in
`drawWorldObjectCore` (egmath.c): the object-vs-eye rel kept the camera's
Q8 fraction (`g_camEyeFrac*`, folded into `setViewPositionFrac`) but the
object's own sub-fine-unit fraction died at the `int32 worldX` parameter —
every model sat up to ±1 fine unit off its true position, alternating with
the accumulator each render frame.

* `drawWorldObject`/`drawAircraftShadow` take `FineRep`/`WordRep` params
  (int32/int16 under fixed — identical values; double under modern).
* The rel position is carried as `FineRep` through all three scale
  branches; the int submit takes `floor` (the original arithmetic `>>`
  semantics) and the dropped remainder joins the eye frac in
  `vfx`/`vfy`/`vfz` — same `pos − frac/256` contract `transformAndCull`
  already documents, mirroring drawTargetView's frac convention
  (Y negated). Under fixed every remainder is 0 and the formulas reduce
  verbatim to the originals.
* Call sites pass the fractional sources: `fineRep(g_ViewX/Y)` for the
  player model, `objectFineRep(g_simObjectFineX/Y)` + `g_simObjectAlt` for
  sim objects, `g_projectileAlt` for projectiles; `g_projInterpX/Y` are
  rep-typed (`fineRep(FineCoord)` fills) so the snapshot interpolation
  reaches the submit — word-domain consumers (`(uint32)` camera target,
  `projectWorldToHudFine` int32) still get the same truncated values.
* `egmath.h` spells the params with the non-gated `FineRep`/`WordRep`
  aliases (horizontal.hpp/rotation.hpp) so the boundary checker needs no
  allowlist entry for the header.

Verification: fixed 60/60 incl. sortie parity (identical arithmetic —
every remainder is provably 0), modern smoke, analyzer clean.

## F10 target-view fraction checkpoint

F10 (`VIEW_TARGET` → `drawTargetView`) had the same boundary loss on its
own path: `int32`/`int` position params truncated the target's sub-fine
fraction, and the tracking camera's aim globals (`g_viewTargetX/Y`,
`g_viewTargetAlt`) were int-typed, so F8/F9/F10's tracked aim quantized
to fine units while the eye kept Q8 precision.

* `drawTargetView` params → `FineRep`/`WordRep`; `dxFine`/`dyFine`/`dzFine`
  are computed fractionally and feed `wideBearing`/`wideRange` and the
  pitch scale directly. Under fixed the fractional terms vanish and the
  expressions reduce verbatim.
* `g_viewTargetX/Y` → `FineRep`, `g_viewTargetAlt` → `WordRep` (egdata);
  `computeTrackingCameraAngles` takes `double` X/Y + `WordRep` altitude.
* `renderFrame` seeds the aim from `fineRep(g_ViewX/Y)` and the typed
  render altitude; projectile aim uses `g_projInterpX/Y` +
  `g_projectileAlt`; sim-object aim uses `objectFineRep` + `g_simObjectAlt`.

Verification: fixed 60/60 incl. sortie parity, modern build + smoke,
analyzer clean (relocated pre-existing findings only).

## Render-interpolated scene height checkpoint (F5–F9 vertical shake fix)

External views shook ~10px vertically under modern: `flightSceneHeight()`
returns the **live per-tick** `g_altitude` under `F15_MODERN_MATH` (fixed
returns the interpolated `g_viewZ`), so `renderFrame`'s camera eye Z stepped
per sim tick while the model's altitude (`g_viewZ`-derived) interpolated per
render frame — a ramp-and-snap sawtooth of the full per-tick climb rate.

* `g_sceneHeightRender` — new `RenderHeight<GameBackend>` global carrying
  the fractional lerp result before `g_viewZ` truncation, written by
  `camApplyInterp` and synced by `camRestore` (egsys.c).
* `renderFrame` camera eye Z and tracking-aim altitude now read
  `g_sceneHeightRender` instead of `flightSceneHeight()`; the F5/F6/F7/F9
  eye-Z paths go through `eyeFromQ8` so the eye keeps its sub-word fraction
  (terrain `lodEyeFracQ8` and model `vfz` compensate identically).
* Player model altitude args pass `render(g_sceneHeightRender) + 0x10`
  (fractional under modern, identical word under fixed).
* Projectile render-interp gate relaxed under modern: interpolate whenever
  both snapshots are the same live slot (`ttl != 0` both sides + teleport
  guard), since the strict `ttl−1` gate skips shots whose update path does
  not decrement every step — leaving `g_projectileAlt` per-tick while the
  tracking eye interpolated. Fixed keeps the original gate verbatim.

Manual verification: F5–F9 and F10 confirmed steady by user testing.

## Near-pole attitude canonicalization checkpoint (vertical-loop fix)

Banked pull-ups could dead-end under modern: exact `atan2` Euler recovery can
hold a knife-edge attitude (|pitch| ≈ 90 deg, |roll| ≈ 90 deg) that the fixed
backend's quantized recovery physically cannot maintain — its churned roll
reading sweeps `g_rollGeeTable` so `loadResponse` intermittently releases pitch
authority. The exact state reads table bins 58–68 (load ≥ 128) continuously,
clamping pitch input to zero; the aircraft parks near-vertical for ~20+ ticks
until the stall drop tumbles it out. User-visible: a loop sometimes overran
the top or fell sideways.

`RotationMath<ModernBackend>::recover` now:

* raises `needsRefresh` on the fixed pole band (|pitch| > 0x38e3 words) and on
  the roll-to-zero transition, matching the fixed trigger semantics;
* at an exact pole (`cos(pitch)` indistinguishable from zero) canonicalizes to
  the fixed convention: roll zero, combined heading from `atan2(-m[6], m[0])`;
* inside the pole band, additionally canonicalizes recovered roll inside the
  g-load clamp window (0x3800–0x4800 words ≈ 78.75–101.25 deg, bracketing
  bins 58–68 with margin) — collapsing bank into heading releases the clamp
  the way fixed churn effectively did. The snap is bounded by the pole
  distance (cos(pitch) ≤ ~0.18 inside the band).

A genuine post-fold inverted attitude recovers |roll| ≈ 180 deg and is left
untouched, so a clean vertical loop still folds over the top exactly as
fixed does.

Verified by replay-driven probes across speed 4500–6500 and roll inputs
0x6e–0xaf: modern knife-edge clamp ticks are now zero or below the fixed
backend's own residual (≤8 vs 15 worst case); the clean centered loop folds
at the same tick as fixed. `typed_rotation_tests` pins the corner contract:
exact recovery everywhere else, knife-edge canonicalized, fold preserved,
sub-window banks untouched. Fixed suite 60/60 including sortie parity.

### Next acceptance boundary

The decision-math surface is migrated end to end: every gameplay compare
that reads a typed source does so through a backend-dispatched helper
(`mapOffset`/`mapRange`/`ringX`, `angleMagnitude`/`angleSeparation`,
`aimBearing`, `wideBearing`/`wideRange`, `limitTurn`, the typed flight
quantities). The remaining raw math falls into three reviewed categories:

* packed-file-layout state (`SimObject` pos/flags/spec/objType, `planes[]`,
  event records) whose words are authoritative — the integrated fields
  (worldX/worldY, heading/pitch/bank, alt/speed) all carry typed shadows,
  so what remains packed has no per-tick fraction to preserve;
* render/HUD/projection internals (view matrix, `scaleCoordToLod` LOD
  quantization, `projectWorldToHud*`, tacmap panning) — the reviewed
  render boundary where word/Q8 outputs are the contract;
* integer bookkeeping (counters, ammo, timers, indices) that is not
  fixed-point math at all.

Before declaring backend equivalence complete, the remaining judgment call
is the modern refresh policy vs the original periodic rebuild, which
intentionally quantizes fixed state, plus any packed-field AI internals the
project chooses to shadow with typed state. The final acceptance
requirements below still apply.

## Render/sim RNG stream separation

`drawWorldEffects` (egtarget.c, per rendered frame via `render3DView`) drew
~26 `randomRange` values per frame for the hit-spark burst — from the same
stream the sim uses for turbulence, AI and spawn rolls. Render frame rate
therefore shifted sim randomness: with render interpolation, multiple (or
zero) renders per sim tick changed how many draws the sim stream saw. The
scatter is re-randomized every frame anyway, so the draws now use a
dedicated render stream — `renderRand15`/`renderRandomRange` in strand.c,
same ANSI LCG shape and `(max * rand15) >> 15` scaling, never recorded,
replayed or clock-seeded. This is a determinism-contract fix, not a
math-representation change; it applies identically to both backends. All
other `randomRange`/`randMul` callers were audited and are sim-rate
(egcombat/egflight/egframe/egthreat/egkeys) or setup-time (stgen mission
generation).

Honest caveat: pre-change blackbox recordings containing active hit bursts
would replay with an `rng` stream mismatch — every subsequent draw shifts.
No `.bbx` recordings are committed and the burst is the only render-path
consumer, so the divergence window is narrow; treat it as a deliberate
contract correction.

Verification: `original_behavior_tests` covers `renderRandomRange` scaling,
range and stream independence (64 interleaved render draws leave the game
sequence untouched); fixed 60/60 and modern 2/2 pass — the harness never
calls the render path, so both sortie goldens are untouched.

## Sortie harness stick injection and the loop profile

The sortie harness's original stick schedule wrote `g_joyRawX/Y` (and later
`joyAxes[]`) after `input_pumpEvents`. Investigation for the loop profile
showed both writes are dead: `kbhit()` inside `stepFlightModel` re-pumps
events and `updateStick()` re-derives `g_joyRaw` from `SDL_GetKeyboardState`,
and `joyAxes` is re-derived from `g_joyRaw` inside the step — and pushed key
events never reach `SDL_GetKeyboardState` at all, ruling out held-key
injection too. The committed sortie golden therefore pins a *centered-stick*
trajectory; its climb/turn/stall phase labels never reached the sim (the
throttle/gear/weapon/autopilot keys did work — they travel the BIOS key ring,
not the stick path).

The harness now attaches a virtual joystick (`SDL_AttachVirtualJoystick`,
2 axes, opened by the real `JOYSTICK_ADDED` -> `joy_open` path). Axis state
persists by construction, so `updateAxes`/`readCalibratedJoystick` deliver
the scheduled byte on every internal pump — the same path real hardware
takes, with the full 0..255 byte range the keyboard cannot express
(`axisForByte` inverts `axisByte`'s deadzone+scale). `kSortie` keeps the
stick centred, preserving its e28b9a4-oracle golden exactly; the `--loop`
profile holds a sustained pull (0xda = stick back) and drives the aircraft
through the pole band — the vertical-loop/pole-fold path that previously
trapped. Both backends traverse: fixed reaches ~15310 words pitch magnitude,
modern reaches 16361 (of 16384 = exactly vertical).

Gates: `sortie_loop_parity_tests` compares per-tick hashes against
`sortie_loop_parity.trace` (recorded at HEAD — the profile exercises the
post-tag pole-fold fix, so a tag recording would pin the original trap) and
asserts non-degeneracy — nonzero stick input inside the pull window, pole
band entered (|pitch| >= 0x3000), altitude range >= 4000 — so a future
injection break fails loudly rather than recording a level flight.
`modern_sortie_loop_tests` pins `sortie_loop_fields_modern.trace` exactly
plus the same assertions; the fixed-envelope/discrete layers are skipped
because pole-band trajectories decorrelate by design and modern takes the
analog input path.

Follow-up option: a stick-active variant of the sortie schedule would change
its trajectory, so its golden would need re-recording at e28b9a4 with a
compat-shimmed harness (the tag predates several typed globals).

## Combat sortie checkpoint (loaded-mission coverage)

The sortie/loop profiles hand-seed `g_planeTable`/`g_simObjects` directly —
the real mission-start path (`initMissionStrings` -> `worldImportToEgame`)
and the combat/AI tick against imported objects had no coverage. The new
`--combat` profile fills the START-side world arrays a mission generator
produces (`worldObjects[]`, `flightUnits[]`, `targets[]`, waypoint globals,
`wldReadBuf*`, `terrainGrid`, counts) with a small strike mission — home
base, primary/secondary targets, four interceptors, a bomb-run striker and a
tracked site — then runs the real import: the +2-byte field-shifted
MapTarget copy, the FlightUnit -> SimObject memcpy plus typed-shadow seeding,
the string-pool parse into `g_targetNameTable`, and the target-anchored
start view. Mission init then runs `findWaypointFeatures`, the wingman seed,
threat scoring and weapon loadout on the imported world.

The schedule flies hands-off: P engages altitude-hold guidance toward
waypoints[1] (the primary target, inside the interceptors' patrol volume),
then S/M/G select weapon slots (AIM-9M/AIM-120/AGM-65 — A2A class 7 and
ground class 6), T designates, RETURN fires, BACKSPACE runs guns, W cycles
waypoints through to the recovery leg. Weapon key presses clear only
`g_autopilotEngaged`; altitude-hold guidance keeps steering throughout.

`combatRequire` asserts non-degeneracy so an empty run cannot pin a golden:
import populated the tables (plane/unit counts, waypoint target, non-empty
name table), autopilot altitude-hold engaged, at least one AI object moved,
the threat alert engaged, and a weapon left the rail (ammo spend, gun spend,
or a live projectile). In practice the run shows locks, launches, gun hits
and incoming damage. `sortie_combat_parity_tests` pins per-tick hashes
(`sortie_combat_parity.trace`, HEAD-recorded — the fixture post-dates
e28b9a4); `modern_sortie_combat_tests` pins `sortie_combat_fields_modern`
exactly plus the same assertions, with the fixed-envelope/discrete layers
skipped because combat trajectories decorrelate through AI retargeting.

The profile closes with `verifyWorldExport()`: the real
`worldExportToEnd()` debrief export runs against the post-mission tables
and every block is checked — plane records round-trip field-for-field
(including the reversed +2-byte unitRef shift), the SimObject block, the
waypoint block, target slots, string pool, category/kill/grid tables,
route/waypoint/SAM counts and the padlock slot all match their live
sources.

Correction recorded here: the sortie schedule's "gear" presses use the G key,
which is actually the Maverick weapon-slot select (L is gear). The labels
were wrong but harmless — the goldens pinned the real behaviour either way.

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
| Angles/trigonometry | Typed fixed/double rotation angles and trig; imported inverse trig | Scalar-trig caller sweep complete: every sim-side bearing/offset goes through `TrackMath`/`CamMath`; the surviving `sinMul`/`cosMul` callers are all display-boundary (HUD markers, spark scatter, seeker dial). `valueToAngle`/`complementAngle`/`signedRatio16` remain as test-pinned frozen oracles, not production callers |
| Fixed products | Q15, rounded result, high-word products and carry variants | Audit every caller's rounding and destination narrowing; Q15 inputs are signed words |
| Long arithmetic | WordPair32, shifts, full/saturated division | Document valid domains, divide-by-zero behavior and overflow policy per operation |
| Orientation | Typed persistent matrix/Euler state, fixed/double recovery, axis deltas and render pose interpolation | Typed control inputs, legacy scalar consumer removal and modern refresh integration |
| Projection/HUD | Selected projection, clipping, HUD rotation helpers | Verify against current rasterizer and HUD state, including invalid/depth sentinels |
| Range/bearing | Legacy approximation and bearing helpers | Keep gameplay distance approximation distinct from Euclidean distance |
| Camera precision | Fine bearing/range and Q8 eye offsets typed; outputs stay word/Q8 at the render boundary | Remaining render-internal projection math (sinMulQ8 remnants in egtacmap, view-matrix LUT path) |
| Terrain/world coordinates | Coarse `MapPosition` typed; sub-LOD precision flows through `g_camEyeFrac*` frac bytes into `lodEyeFracQ8` | scaleCoordToLod LOD quantization is render-internal; all nearest-tile callers pass packed word sources |
| Flight integration | stepFlightModel forces, velocity/position integration, coefficients and clamps typed end to end | Refresh policy settled: `recover().needsRefresh` ports the original pole-band + roll→0 triggers, so both backends rebuild the matrix on the same cadence |
| Combat/AI | Projectile guidance/state, bullet tracks, SimObject decision reads, acquisition, lock cones, corridor gates, fine-position shadow, attitude shadow (heading/pitch/bank), alt/speed shadows (SimObject + flag-aliased Projectile.alt) typed | Hit-test broad phases are word-domain game rules (precise swept test already typed); packed words remain synced render/serialization mirrors (Projectile.alt bit0 = radar flag) |
| Randomness/time | Scaling helper; render-side draws split onto a dedicated non-checked stream so render frame rate cannot shift sim RNG; full audit confirms every sim-stream caller is sim-tick/init-path | Frame pacer accumulators reviewed: event counts and derived ratios, legitimately plain counters |

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

Altitude-hold target storage is now `RenderHeight<GameBackend>` in both builds.
Capturing a modern target uses continuous flight altitude, preserving fractional
scene height above the old signed-word range. The fixed capture policy is checked
over every signed 16-bit input; modern key-dispatch coverage checks capture and
toggle-off at altitude 131072.25. Zero remains the existing inactive sentinel.
Input cancellation, mission resets, recovery setup, and map indicators use the
typed state rather than implicit integer assignments or comparisons.

Flight-model ground equality and airborne tests now compare typed scene and
terrain heights. Fixed builds preserve the stored word and the original unsigned
ordering where used. Modern builds derive current height from continuous flight
altitude. Production braking tests cover actual ground, a fractional airborne
height, and the scene-word wrap at altitude 229376; the high-altitude flight-loop
regression covers both 131072 and 229376.

Height-decision consumers of `g_viewZ` are now migrated through
`flightSceneHeight()`: the stall warning (`egflight.c`), the mission ground
check (`missionAtHeight` in `egframe.c`), the gear-toggle ground check and
autopilot capture (`egkeys.c`), the stall-text HUD check (`egtacmap.c`), and the
eject-draw suppression and shadow band checks (`egtarget.c`). `CamSnapshot`
stores a typed `RenderHeight<GameBackend>` and interpolates via
`AltitudeMath::interpolate`/`belowSceneHeight`, narrowing back to `g_viewZ` only
through `AltitudeBoundary::renderWord` (a defined mod-2^16 wrap). Legacy
word-domain storage remains by design: `g_viewZ` itself and the `.alt` fields of
`ViewSnapshot`, `Particle`, `Projectile` and `SimObject` are frozen original
binary layouts consumed by the word-space renderer, so they still receive the
boundary word.

Every remaining non-render `g_viewZ` read now sources the typed scene height
through explicit boundary adapters instead of the stored word: word-domain
storage and formulas use `AltitudeBoundary::renderWord` (camera eye/target
globals, `Particle`/`ViewSnapshot` alt writes, `g_hitAlt`, `g_threatRefZ`, SAM
acquisition and projectile launch altitude, LOD depth-shift and hit-distance
formulas, bullet-track spawn altitude, scope altitude diff); integer-domain
decisions use `Altitudes::render` so modern sees the unwrapped value (threat
target-Z clamps, missile lock-range gate); and the deck-level shadow check
compares typed `RenderHeight` equality. Projectile launch speed uses
`AirspeedBoundary::projectile` — `speedWord >> 11` under fixed, the unwrapped
quotient saturated to the `int16` field under modern (velocity 70000 gives 34,
not the wrapped 2). `VerticalQuantity` gained same-unit `==`/`!=`. The word-domain
projectile guidance internals (`sinMul`/`cosMul` angle words, byte reads, `fineX`
integration) and the render-pipeline reads (`drawWorldObject`,
`projectWorldToHud`, `egmath.c`/`egtgt2.c` projection) still read `g_viewZ`
directly — they belong to the world-coordinate feature, and `SimObject`'s packed
`FLIGHTUNIT_SIZE` layout stays frozen regardless.

Remaining `g_viewZ` work is the renderer/world coordinate space itself —
projection, terrain and targeting globals, plus the fixed-format `.alt` fields
above — not further decision plumbing. These tests do not establish
unrestricted high-altitude flight or rendering stability.

For whole-sortie migration, capture a fixed seed, initial state and tick-indexed
inputs. Compare fixed-backend state after each tick exactly. For floating point,
use declared angular/position/velocity tolerances, finite-value checks, matrix
orthogonality and scenario-level outcomes. Cover takeoff, turns, stall,
landing, missile pursuit, wrap boundaries and long flights. Rendering FPS must
not determine simulation step count or outcomes. Do not claim that a handful
of matching scalar tests guarantees identical missions.

Current state vs that checklist: the scripted sortie harness now exists and
both backends run it (fixed exact golden; modern pin + envelope + discrete
transitions, see the acceptance section). Still missing: loaded mission
assets, landing and missile-pursuit outcomes, wrap-boundary scenarios,
interactive `.bbx` recordings, real-time pacing, and multi-platform runs —
so modern coverage remains a scripted-profile acceptance gate, not a
certification of the whole game.

Reproduce standalone Clang checks from the repository root:

```sh
clang++ --analyze -std=c++17 -Isrc -Xanalyzer -analyzer-output=text tests/fixed_math_tests.cpp
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc tests/fixed_math_tests.cpp -o build/math-clang-sanitized
./build/math-clang-sanitized
ctest --test-dir build --output-on-failure
```
