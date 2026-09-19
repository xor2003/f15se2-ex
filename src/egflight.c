#include "math/legacy_horizontal.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_propulsion.hpp"
using f15::math::legacy::thrustFromUnits;
using f15::math::legacy::thrustUnits;
using Propulsion = f15::math::PropulsionMath<f15::math::GameBackend>;
#include "math/aerodynamics.hpp"
#include "math/guidance.hpp"
#include "math/legacy_map.hpp"
using f15::math::legacy::speedWord;
using f15::math::legacy::speedFromUnits;
using SpeedMath = f15::math::AirspeedMath<f15::math::GameBackend>;
using Aero = f15::math::AerodynamicsMath<f15::math::GameBackend>;
using f15::math::legacy::fineUnits;
#include "eg3dview.h"
#include "egcode.h"
#include "egdata.h"
#include "egflight.h"
#include "android_ar.h"
#include "game_options.h"
#include "egframe.h"
#include "egkeys.h"
#include "egmath.h"
#include "egpic.h"
#include "egtacmap.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "gfx.h"
#include "r2d.h"
#include "slot.h"
#include "const.h"
#include "comm.h"
#include "eginput.h"
#include "input.h"
#include "joystick.h"
#include "math/legacy_rotation.hpp"
#include "math/legacy_flight_control.hpp"
#include "math/legacy_altitude.hpp"
using f15::math::legacy::altitudeUnits;
using f15::math::legacy::climbUnits;
using f15::math::legacy::signedAngle;
using f15::math::legacy::angleFromWord;
using f15::math::legacy::rollCommand;
using f15::math::legacy::pitchCommand;
using f15::math::legacy::rollInput;
using f15::math::legacy::pitchInput;

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
void stepFlightModel();
void applyRotationDelta(const f15::math::Matrix3<f15::math::GameBackend> &matA,
                        const f15::math::Matrix3<f15::math::GameBackend> &matB);
void computeAttitudeAngles(void);
void rebuildOrientation();
uint16 signedRatio16(int16, int16);
int valueToAngle(int value);
int complementAngle(int value);
void renderFrame();
void drawVectorShape(const int16 *shapeData);
void waitForKeyPress(void);

void advanceFlightOrientation(const f15::math::RotationDeltas<f15::math::GameBackend> &deltas) {
    const f15::math::legacy::Math math(g_angleLut);
    const auto advanced = f15::math::FlightControlMath<f15::math::GameBackend>::advanceOrientation(
        math, g_orientMatrix, deltas);
    for (unsigned i = 0; i < advanced.products; ++i) {
        ++g_rotationCounter;
        if (!(static_cast<uint16>(g_rotationCounter) & 7)) g_orientationDirty = 1;
    }
    if (advanced.products) g_matrixScratch = advanced.matrix;
    g_orientMatrix = advanced.matrix;
    computeAttitudeAngles();
}

void advanceFlightHorizontal(f15::math::HorizontalSpeed<f15::math::GameBackend> speed) {
    if (g_autoLandingActive != 0) return;
    const f15::math::legacy::Math rotation(g_angleLut);
    const auto step = f15::math::HorizontalMath<f15::math::GameBackend>::increments(
        speed, rotation.sine(g_ourHead), rotation.cosine(g_ourHead),
        f15::math::legacy::Controls::frequency(g_frameRateScaling));
    g_ViewX += step.x;
    g_ViewY += step.y;
}

void advanceFlightAltitude() {
    using VerticalMath = f15::math::AltitudeMath<f15::math::GameBackend>;
    using Altitudes = f15::math::legacy::Altitudes;
    const f15::math::legacy::Math rotation(g_angleLut);
    g_climbRate = VerticalMath::climb(SpeedMath::verticalSample(g_velocity),
        rotation.sine(g_ourPitch - g_rollPitchTrim));
    if (g_autoLandingActive == 0)
        g_altitude = VerticalMath::integrate(g_altitude, g_climbRate,
            f15::math::legacy::Controls::frequency(g_frameRateScaling));
    g_altitude = VerticalMath::constrain(g_altitude, Altitudes::ground(g_groundAltitude));
    g_viewZ = Altitudes::render(VerticalMath::renderHeight(g_altitude));
}

void updateFlightLift() {
    const auto lift = Aero::liftCorrection(g_stallSpeed, g_velocity);
    const auto trim = Aero::pitchTrim(lift, f15::math::legacy::Math(g_angleLut).cosine(g_ourRoll));
    g_liftForce = lift;
    g_rollPitchTrim = trim;
}

bool correctFlightStall() {
    if ((uint16)g_groundAltitude >= (uint16)g_viewZ || !Aero::belowStall(g_velocity, g_stallSpeed)) return false;
    const auto severity = gameData->unk4 == 2 || g_gunHits > 8
        ? f15::math::StallSeverity::Severe : f15::math::StallSeverity::Normal;
    const auto response = Aero::stallResponse(g_velocity, g_stallSpeed, severity,
        f15::math::legacy::Controls::frequency(g_frameRateScaling));
    g_ourPitch -= response.noseDrop;
    g_orientationDirty = 1;
    return response.stalled;
}

void accelerateFlightSpeed(f15::math::FlightSpeed<f15::math::GameBackend> target) {
    g_velocity = SpeedMath::accelerate(g_velocity, target,
        f15::math::legacy::Controls::frequency(g_frameRateScaling));
}

void brakeFlightSpeed() {
    if (*((uint8 *)&g_playerPlaneFlags) & 8) {
        const auto step = f15::math::legacy::Controls::frequency(g_frameRateScaling);
        if (g_groundAltitude == g_viewZ) {
            g_velocity = SpeedMath::groundBrake(g_velocity,
                f15::math::legacy::Airspeeds::deceleration((32 - gameData->unk4 * 8) * 27), step);
            if (g_groundAltitude != 0) g_velocity = SpeedMath::carrierStop(g_velocity);
        } else g_velocity = SpeedMath::airBrake(g_velocity, step);
    }
    g_velocity = SpeedMath::constrain(g_velocity);
}

void stepFlightModel(void) {
    // Local variables - names chosen to match MSC 5.1 hash-based stack layout
    // Within same hash bucket: first declared → highest BP offset (LIFO)
    // Dummies fill gaps to achieve sub sp, 0x3E (31 word slots)
    int16 p;                                // dummy:  bp-0x02 (bucket 0)
    int16 a, q;                             // dummies: bp-0x04, bp-0x06 (bucket 1)
    int16 prevAlt, aa, r;                   // var_C=prevAlt at bp-0x0c, dummies at bp-0x0a,bp-0x08 (bucket 2)
    int16 ab, tgtIdx;
    int16 tmpVal;
    int16 ad, u;                           // dummies at bp-0x1e,bp-0x1c (bucket 5)
    int16 turbulence;
    f15::math::HorizontalSpeed<f15::math::GameBackend> horizVel;
    int16 w;
    int16 headingErr;
    int16 i, y;                             // dummies: bp-0x2e, bp-0x30 (bucket 9)
    int16 k;                                // dummy:  bp-0x36 (bucket 11)
    int16 idx;                              // var_38: bp-0x38 (bucket 12)
    int16 m;                                // dummy:  bp-0x3a (bucket 13)
    int androidFlightControl = 0;
    int pointerThrottle = 0;

    if (g_initPhase == 0) {
        g_ourPitch = g_ourRoll = {};
        g_altitude = {};
        g_velocity = {};
        g_viewZ = g_setThrust = 0;
        g_thrust = {};

        if (gameData->difficulty == 0) {

            g_ourHead = angleFromWord(((g_viewY_ - (waypoints[1].mapY)) < 0x8000) ? 0 : 0x8000);
        } else {
            g_ourHead = angleFromWord((gameData->theater == 6)
                            ? 0                                       // If true, the result is 0
                            : ((gameData->theater & 1) ? 0 : 0x8000)); // Else, evaluate the second condition
        }

        if (g_planeTable.planes[g_targetSlots[0].viewIndex].flags & 0x200) {
            g_ourHead += angleFromWord(0x400);
        }

        rebuildOrientation();
        UpdateThrottleState();
        g_initPhase = 1;
    }

#if defined(__ANDROID__)
    /* Publish the aircraft attitude before input sampling. Android uses it as
     * feedback for target-angle controls, never as an integrated turn rate. */
    android_ar_setAutopilotActive(g_autopilotAltitude != 0 ||
                                  g_autopilotEngaged != 0);
    android_ar_setGameAttitude(signedAngle(g_ourPitch), signedAngle(g_ourRoll));
#endif

    keyScancode = 0;
    if (kbhit()) {
        keyScancode = egReadKey();
        if (g_autopilotEngaged == 1) {
            g_directorMode =
                g_autopilotEngaged =
                    g_viewMode = VIEW_COCKPIT;
        }
    }

    while (kbhit()) {
        egReadKey(); // Flush keyboard buffer
    }

    if (input_takeFlightThrottle(&pointerThrottle) && !joy_hasThrottleAxis()) {
        /* Touch uses the same target-thrust state and gauge update as the
         * original +/- keys; only the input device is modern. */
        g_setThrust = clampRange(pointerThrottle, 0, 100);
        UpdateThrottleState();
        *((uint8 *)&g_playerPlaneFlags) &= 0xF7; /* release wheel brakes */
        if (g_autopilotEngaged == 1) {
            g_directorMode =
                g_autopilotEngaged =
                    g_viewMode = VIEW_COCKPIT;
        }
    }

    /* Raw-stick commands bypass the keyboard flush above. Reuse the existing
     * dispatch so keyboard and joystick actions have identical game effects. */
    if (keyScancode == 0) {
        keyScancode = joy_flightCommand(missileSpecIndex, g_viewMode);
        if (keyScancode != 0 && g_autopilotEngaged == 1) {
            g_directorMode = g_autopilotEngaged = g_viewMode = VIEW_COCKPIT;
        }
    }
    {
        const int throttle = joy_throttleChange();
        if (throttle >= 0 && g_inputDisabled == 0) {
            g_setThrust = throttle;
            UpdateThrottleState();
            if (throttle > 0) g_playerPlaneFlags &= ~8;
        }
    }

    // Main key dispatch logic
    switch ((uint16)keyScancode) {
    case INPUT_KEY_LOOK_ON:
        hudMessage("Look on");
        goto switch_break;
    case INPUT_KEY_LOOK_OFF:
        if (g_autopilotAltitude != 0 || g_autopilotEngaged != 0)
            hudMessage("Look blocked: autopilot on");
        else
            hudMessage("Look off");
        goto switch_break;
    case SCAN_MINUS:
        g_setThrust = clampRange(g_setThrust - 10, 0, 100);
        UpdateThrottleState();
        goto switch_break;
    case SCAN_EQUAL:
        g_setThrust = clampRange(g_setThrust + ((g_setThrust < 10) ? 5 : 10), 0, 100);
        UpdateThrottleState();
        *((uint8 *)&g_playerPlaneFlags) &= 0xF7; // ~8
        goto switch_break;
    case SCAN_A:
        g_setThrust = 0x90;
        UpdateThrottleState();
        *((uint8 *)&g_playerPlaneFlags) &= 0xF7; // ~8
        goto switch_break;
    case SCAN_SHIFT_EQUAL:
        g_setThrust = 100;
        UpdateThrottleState();
        *((uint8 *)&g_playerPlaneFlags) &= 0xF7; // ~8
        goto post_key_B_check;
    case SCAN_SHIFT_MINUS:
        g_setThrust = 0;
        makeSound(16, 0);
        UpdateThrottleState();
        goto switch_break;
    case SCAN_B:
        *((uint8 *)&g_playerPlaneFlags) ^= 8;
    post_key_B_check:
        if (!(*((uint8 *)&g_playerPlaneFlags) & 8) && g_groundAltitude != 0 && g_setThrust == 100) {
            g_velocity = speedFromUnits(1350);
            makeSound(28, 2);
        }
        goto switch_break;
    case SCAN_ALT_J:
        if (g_joyCalibTimer == 0) {
            initJoystickCalibration();
            g_joyCalibTimer = 40;
        }
        goto switch_break;
    case SCAN_ALT_Q:
        finalizeMission(1);
        exitCode = 0;
        goto switch_break;
    case SCAN_ALT_B:
        if (g_hudVisible != 0) {
            gfx_captureToImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
        }
        setDrawColor(COLOR_BLACK);
        fillRectBoth(0, 0, 319, 199);
        blitSprite(0, 0, 113, 55, 12, 7, 0);
        waitForKeyPress();
        if (g_hudVisible != 0) {
            gfx_restoreFromImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
            UpdateThrottleState();
        }
        goto switch_break;
    case SCAN_ALT_P:
        waitForKeyPress();
        goto switch_break;
    }

switch_break:
    if (g_joyCalibTimer != 0) {
        g_joyCalibTimer--;
    }

    if (g_setThrust != 0 && g_thrust.isZero()) {
        makeSound(14, 2);
    }

    if (g_inputDisabled != 0) {
        joyAxes[0] = 0;
        joyAxes[1] = 0;
#if defined(__ANDROID__)
    } else if (g_autopilotAltitude != 0 || g_autopilotEngaged != 0) {
        /* A sensor sample may already be cached when autopilot is toggled.
         * Neutralize it here so legacy stick input cannot cancel autopilot. */
        joyAxes[0] = 0x80;
        joyAxes[1] = 0x80;
#endif
    } else {
        if (input_preferGamepad()) {
            readCalibratedJoystick();
        } else {

            // temp_si = g_kbdSensitivity + 1;
            joyAxes[0] = (uint8)(((int16)((uint8)g_joyRawX - 0x80) * (g_kbdSensitivity + 1)) / 3) - 0x80;

            joyAxes[1] = (uint8)(((int16)((uint8)g_joyRawY - 0x80) * (g_kbdSensitivity + 1)) / 3) - 0x80;
        }
    }

    using Controls = f15::math::legacy::Controls;
    using ControlMath = f15::math::FlightControlMath<f15::math::GameBackend>;
    const auto commands = ControlMath::fromJoystick(Controls::joystick(joyAxes[0], joyAxes[1]));
    g_rollInput = commands.roll;
    g_pitchInput = commands.pitch;

    /*
     * Android attitude control needs corrections smaller than the DOS
     * joystick's nibble-sized bins. Keep the legacy path intact, then replace
     * only its final flight inputs while the optional camera controller is on.
     */
    /* A deliberately enabled altitude autopilot owns the controls until the
     * player toggles it off; otherwise handset attitude is authoritative. */
    /* Direct attitude must not cancel a stall, ground constraint, or forced
     * crash. Zero thrust alone is not a stall: unpowered gliding remains valid. */
    const bool attitudeControlAllowed =
        g_autopilotAltitude == 0 && g_inputDisabled == 0 &&
        g_ejectState == 0 && g_autoCrashDive == 0 &&
        Aero::aboveStall(g_velocity, g_stallSpeed) &&
        (g_groundAltitude != g_viewZ || g_knots >= g_cornerSpeed);
    androidFlightControl = attitudeControlAllowed
                               ? f15::math::legacy::updateControlFromWords(
                                     g_rollInput, g_pitchInput, android_ar_overrideFlightInput)
                               : 0;
    if (androidFlightControl) {
        g_autopilotAltitude = 0;
        g_autopilotEngaged = 0;
        g_directorMode = 0;
        /*
         * Phone tilt is a requested attitude, not a virtual stick rate. Build
         * the legacy matrix from that attitude so its yaw/lift calculations
         * remain intact without a feedback loop chasing decoded Euler jumps.
         */
        if (f15::math::legacy::updateAttitudeFromWords(g_ourRoll, g_ourPitch, android_ar_overrideFlightAttitude)) {
            rebuildOrientation();
        }
    }

    if (g_groundAltitude == g_viewZ && g_pitchInput.isNegative() && signedAngle(g_ourPitch) <= 0) {
        g_pitchInput = {};
    }

    if (g_knots > 350 && !(*((uint8 *)&g_playerPlaneFlags) & 1) && g_gearDownArmed != 0) {
        g_gearDownArmed = 0;
        *((uint8 *)&g_playerPlaneFlags) |= 1;
        hudMessage("Landing gear raised");
        makeSound(32, 2);
    }

    if (g_groundAltitude == g_viewZ && g_setThrust == 0 && !(*((uint8 *)&g_playerPlaneFlags) & 8)) {
        *((uint8 *)&g_playerPlaneFlags) |= 8;
        hudMessage("Brakes on");
    }

    if (!g_rollInput.isZero() || !g_pitchInput.isZero()) {
        g_autopilotAltitude = 0;
    }

    if (g_autopilotAltitude != 0) {
        const auto headingOffset = angleFromWord(g_autopilotEngaged != 0 ?
            (g_missionTick & 0xF) * 256 - 2048 : 0);
        const auto guidance = f15::math::GuidanceMath<f15::math::GameBackend>::altitudeHold(
            f15::math::legacy::renderHeightFromUnits(g_autopilotAltitude),
            f15::math::legacy::renderHeightFromUnits(g_viewZ),
            {g_ourHead, g_ourPitch, g_ourRoll}, angleFromWord(g_waypointBearing), headingOffset, g_rollPitchTrim);
        g_rollInput = guidance.roll;
        g_pitchInput = guidance.pitch;

        if (waypointIndex == 3) {
            tgtIdx = g_targetSlots[1].viewIndex;
            const int inRecoveryCorridor = g_inLandingCorridor != 0 &&
                g_closestThreatIndex == tgtIdx;

            const auto approach = f15::math::GuidanceMath<f15::math::GameBackend>::recoveryApproach(
                f15::math::legacy::mapPosition(g_planeTable.planes[tgtIdx].mapX, g_planeTable.planes[tgtIdx].mapY),
                f15::math::legacy::mapPosition(g_viewX_, g_viewY_), g_ourHead,
                (g_planeTable.planes[tgtIdx].flags & 0x200) != 0,
                static_cast<f15::math::RecoveryDirection>(g_northSouthSign), inRecoveryCorridor != 0);
            if (approach.exitSlowMotion) exitSlowMotion();
            *((uint8 *)&g_playerPlaneFlags) &= 0xF7;
            if (approach.allowBrakes && g_setThrust * 80 < g_knots) *((uint8 *)&g_playerPlaneFlags) |= 8;
            const auto bankTarget = f15::math::GuidanceMath<f15::math::GameBackend>::recoveryBank(
                approach.bearing, g_ourHead,
                f15::math::legacy::speedFromUnits(g_knots * 27), inRecoveryCorridor != 0);

            const auto recovery = f15::math::GuidanceMath<f15::math::GameBackend>::recoveryAttitude(
                approach.height,
                f15::math::legacy::renderHeightFromUnits(g_viewZ),
                {g_ourHead, g_ourPitch, g_ourRoll}, bankTarget, g_rollPitchTrim);
            g_rollInput = recovery.roll;

            g_setThrust = f15::math::legacy::thrustUnits(
                f15::math::GuidanceMath<f15::math::GameBackend>::recoveryThrust(
                    bankTarget, approach.height));
            UpdateThrottleState();

            g_pitchInput = recovery.pitch;

            if (g_knots < 350) {
                *((uint8 *)&g_playerPlaneFlags) &= 0xFE;
            }

            if (g_groundAltitude == g_viewZ) {
                g_setThrust = 0;
                g_rollInput = {};
                g_playerPlaneFlags |= 8;
                g_pitchInput = {};
            }
        }
    }
    if (gameData->unk4 != 0) {
        turbulence = ((int32)g_knots * (1000 - g_viewZ)) >> 15;
    } else {
        turbulence = 0;
    }

    if (!((g_playerPlaneFlags) & 1)) {
        turbulence += clampRange((g_knots - 200) >> 5, 0, 32);
    }

    if (turbulence > 0 && ((uint16)g_groundAltitude) < ((uint16)g_viewZ)) {
        g_rollInput += rollCommand(randomRange(turbulence) - (turbulence >> 1));
        g_pitchInput += pitchCommand((randomRange(turbulence) - (turbulence >> 1)) >> 1);
    }

    if ((g_playerPlaneFlags & 1) && (g_pitchInput.isNegative() || g_pitchInput.isZero()) && Aero::aboveStall(g_velocity, g_stallSpeed) && gameData->unk4 < 2 && abs((int16)signedAngle(g_ourRoll)) < 0x3000 && g_gunFiredFlag == 0) {
        tmpVal = (((signedAngle(g_rollPitchTrim) - signedAngle(g_ourPitch)) >> 2) - g_viewZ + 300) >> 2;
        if (tmpVal > 0) {
            g_pitchInput = pitchCommand(clampRange(tmpVal, 0, 32));
        }
    }

    if (g_ejectState != 0) {
        g_rollInput = rollCommand(0x40);

        g_pitchInput = pitchCommand((abs((int16)signedAngle(g_ourRoll)) > 0x4000) ? 0x10 : -8);

        // g_ejectState++;
        g_crashCamZ += clampRange(
            -(++g_ejectState - 0x20),
            (int16)0xFF00 / g_frameRateScaling,
            (int16)0x80 / g_frameRateScaling);
        // g_crashCamZ += stall_decay_effect;

        if (g_crashCamZ < 0) {
            g_crashCamZ = 0;
            if ((g_missionTick & 7) == 0) {
                finalizeMission(0);
            }
        }

        if (g_viewZ == 0 && g_smokeSourceIdx == -1) {
            g_smokeSourceIdx = 0;
            g_planeTable.planes[0].mapX = g_viewX_;
            g_planeTable.planes[0].mapY = g_viewY_;
            g_hitMapX = g_viewX_;
            g_hitMapY = g_viewY_;
            g_hitAlt = 0;
            g_hitEffectTimer = -8;
            makeSound(2, 2);
            g_velocity = {};
            g_setThrust = 0;
        }

        if ((g_ejectState & 0xFFFC) == 0x10 && (frameTick & 3) == 1) {
            g_smokeSourceIdx = -1;

            idx = ((uint16)frameTick / 2) & 7;

            g_particles[idx].posX = g_viewX_;
            g_particles[idx].posY = g_viewY_;
            g_particles[idx].alt = g_viewZ;

            g_particles[idx].spin = randomRange(0x20) << 11;

            g_smokeParticleSlot = idx;
            g_hitMapX = g_viewX_;
            g_hitMapY = g_viewY_;
            g_hitAlt = g_viewZ;
            g_hitEffectTimer = -8;
            makeSound(0, 2);

            g_ourPitch = -f15::math::Angle<f15::math::GameBackend>::quarterTurn();
            g_orientationDirty = 1;
        }
    }

    const auto requestedThrust = thrustFromUnits(g_setThrust);
    const auto limitedThrust = Propulsion::limitForDamage(requestedThrust, g_gunHits);
    if (Propulsion::requiresDamageLimit(requestedThrust, g_gunHits)) {
        g_setThrust = thrustUnits(limitedThrust);
        UpdateThrottleState();
    }

    g_thrust = Propulsion::advance(g_thrust, limitedThrust, Controls::frequency(g_frameRateScaling));

    if ((((uint16)frameTick) % ((uint16)(g_frameRateScaling << 1))) == 0 && g_setThrust != 0 && g_autopilotEngaged == 0) {
        if (!gameOptionsEnabled(GAME_OPTION_INFINITE_FUEL))
            g_fuelRemaining -= ((g_setThrust * g_setThrust) / 750) + 2;
        drawFuelGauge();
    }

    if (g_fuelRemaining <= 0) {
        g_thrust = {};
        g_fuelRemaining = 0;
    }

    const auto load = Aero::loadResponse(Aero::bankLoad(g_ourRoll, g_rollGeeTable), g_pitchInput,
        ((uint16)g_groundAltitude) < ((uint16)g_viewZ));
    g_gees = load.load;
    g_pitchInput = load.pitch;

    // Display/debug output uses legacy units.
    const int legacyLoad = f15::math::legacy::loadSixteenths(g_gees);
    strcpy(g_geeStringBuf, itoa(legacyLoad / 16, strBuf, 10));
    strcat(g_geeStringBuf, ".");

    strcat(g_geeStringBuf, itoa((abs(legacyLoad) & 0xF) >> 1, strBuf, 10));
    strcat(g_geeStringBuf, "G");

    const auto corner = Aero::cornerSpeed(g_altitude, load.load);
    g_cornerSpeed = f15::math::legacy::cornerKnots(corner);
    g_stallSpeed = Aero::stallThreshold(corner);
    accelerateFlightSpeed(Propulsion::targetSpeed(g_thrust,
        f15::math::legacy::Math(g_angleLut).sine(g_ourPitch),
        f15::math::legacy::renderHeightFromUnits(g_viewZ),
        f15::math::legacy::fuelFromUnits(g_fuelRemaining),
        load.load,
        (g_playerPlaneFlags & 1) ? f15::math::LandingGear::Retracted : f15::math::LandingGear::Extended));

    updateFlightLift();

    brakeFlightSpeed();

    horizVel = SpeedMath::horizontalSample(g_velocity, f15::math::legacy::Math(g_angleLut).cosine(g_ourPitch));
    g_knots = speedWord(g_velocity) / 27;

    audio_setEnginePitch(g_knots, thrustUnits(g_thrust));

    const f15::math::legacy::Math turnRotation(g_angleLut);
    auto yaw = Aero::turnRate(g_gees, g_velocity,
        turnRotation.sine(g_ourRoll), turnRotation.cosine(g_ourPitch));

    /*
     * Turbulence and other legacy assists modify the stick earlier in this
     * routine. Reapply the phone's target-attitude command at the final normal
     * flight-control boundary so those later additions cannot shake the
     * aircraft away from the handset pose. Ground steering and forced crash
     * behavior below remain authoritative.
     */
    if (androidFlightControl) {
        f15::math::legacy::updateControlFromWords(g_rollInput, g_pitchInput, android_ar_overrideFlightInput);
    }

    if (g_groundAltitude == g_viewZ) {
        yaw = ControlMath::groundYaw(g_rollInput);
        g_rollInput = {};
        if (g_knots < g_cornerSpeed) {
            g_pitchInput = {};
        }
    }

    if (g_autoCrashDive != 0) {
        g_pitchInput = pitchCommand(-0x400 - signedAngle(g_ourPitch));
        g_velocity = {};
        g_setThrust = 0;
    }

#if defined(__ANDROID__)
    android_ar_setFlightDebug(signedAngle(g_ourHead), Controls::yaw(yaw), rollInput(g_rollInput), pitchInput(g_pitchInput),
                              g_knots, legacyLoad, turbulence,
                              g_autopilotAltitude, g_autopilotEngaged,
                              g_directorMode, g_frameRateScaling);
#endif

    const auto deltas = ControlMath::increments(g_rollInput, g_pitchInput, yaw,
                                               Controls::frequency(g_frameRateScaling));
    if (androidFlightControl) {
        /*
         * Phone tilt specifies only bank and pitch. Advance heading from the
         * original aerodynamic yaw directly; decomposing the fixed-point
         * matrix back to Euler angles can alternate between equivalent
         * azimuths and make a banked aircraft shake toward its old heading.
         */
        g_ourHead += deltas.yaw;
        f15::math::legacy::updateAttitudeFromWords(g_ourRoll, g_ourPitch, android_ar_overrideFlightAttitude);
        g_orientationDirty = 1;
    } else {
        advanceFlightOrientation(deltas);
    }

    if (correctFlightStall()) {
        if (signedAngle(g_ourPitch) < 0 || (uint16)g_viewZ < 200) {
            makeSound(20, 1);
        }
    }

    if (g_groundAltitude == g_viewZ) {
        if (signedAngle(g_ourRoll) != 0) {
            g_ourRoll = {};
            g_orientationDirty = 1;
        }
        if (signedAngle(g_ourPitch) < 0 || (signedAngle(g_ourPitch) > 0 && g_knots < g_cornerSpeed)) {
            if (g_autoCrashDive == 0) {
                g_ourPitch = {};
            }
            g_orientationDirty = 1;
        }
    }

    g_autoCrashDive = 0;

    g_highGeeFlag[0] = ((abs(signedAngle(g_ourPitch))) - (abs((int16)signedAngle(g_ourRoll)) / 2) > 0x1000) ? 1 : 0;

    /* Keep the stall and ground corrections above. Reapplying handset attitude
     * here would erase their nose drop and permit flight below stall speed. */

    if (g_orientationDirty) {
        rebuildOrientation();
    }

    prevAlt = altitudeUnits(g_altitude);
    advanceFlightAltitude();

    advanceFlightHorizontal(horizVel);

    if (g_groundAltitude == g_viewZ) {
        if (prevAlt > g_groundAltitude && g_inLandingCorridor != 0) {
            makeSound(12, 2);
            // temp_bx = g_closestThreatIndex << 4;

            if (!android_ar_preventCrashes() &&
                !gameOptionsEnabled(GAME_OPTION_NO_DAMAGE) &&
                (((((g_planeTable.planes[g_closestThreatIndex].flags & 0x200) ? 0x100 : 0x80) < ((int16)(-climbUnits(g_climbRate) * g_missionStatus) / 2))) ||
                ((gameData->unk4 != 0 &&
                  (((g_playerPlaneFlags & 1) != 0) ||
                   (((int16)abs(signedAngle(g_ourRoll))) > (int16)((0x30 / (g_missionStatus + 1)) << 8))))))) {
                makeSound(0, 2);
                waitFrameSync(60);
                finalizeMission(5);
            }
        }
        g_climbRate = {};
    }

    idx = frameTick & 0xF;
    g_viewSnapshotRing[idx].heading = signedAngle(g_ourHead);
    g_viewSnapshotRing[idx].pitch = signedAngle(g_ourPitch);
    g_viewSnapshotRing[idx].roll = signedAngle(g_ourRoll);
    *(int32 *)&g_viewSnapshotRing[idx].worldX = fineUnits(g_ViewX);
    *(int32 *)&g_viewSnapshotRing[idx].worldY = fineUnits(g_ViewY);
    g_viewSnapshotRing[idx].alt = g_viewZ;

    if (g_currentWeaponType == 1) {
        if (g_airTargetLock >= 0) {
            idx = clampRange((rangeApprox(g_viewX_ - g_simObjects[g_airTargetLock].posX, g_viewY_ - g_simObjects[g_airTargetLock].posY) * g_frameRateScaling) >> 8, 0, 12);

        } else {
            idx = g_frameRateScaling - 1;
        }

        idx = (frameTick - idx) & 0xF;

        headingErr = signedAngle(g_ourHead) - g_viewSnapshotRing[idx].heading;
        tmpVal = signedAngle(g_ourPitch) - g_viewSnapshotRing[idx].pitch;

        g_aamSeekerX = cosMul(signedAngle(g_ourRoll), ((-headingErr) >> 2)) + sinMul(signedAngle(g_ourRoll), (tmpVal >> 2));

        g_aamSeekerY = sinMul(signedAngle(g_ourRoll), (headingErr >> 2)) + cosMul(signedAngle(g_ourRoll), (tmpVal >> 1));
    }
}

void applyRotationDelta(const f15::math::Matrix3<f15::math::GameBackend> &matA,
                        const f15::math::Matrix3<f15::math::GameBackend> &matB) {
    g_rotationCounter++;
    if (!(static_cast<uint16>(g_rotationCounter) & 7)) {
        g_orientationDirty = 1;
    }
    g_matrixScratch = matA * matB;
    g_orientMatrix = g_matrixScratch;
}

void computeAttitudeAngles(void) {
    namespace legacy = f15::math::legacy;
    const legacy::Math math(g_angleLut);
    const auto recovered = math.recover(g_orientMatrix, g_rollWasNonzero != 0);
    g_ourHead = recovered.angles.yaw;
    g_ourPitch = recovered.angles.pitch;
    g_ourRoll = recovered.angles.roll;
    if (recovered.needsRefresh) g_orientationDirty = 1;
}

void rebuildOrientation() {
    namespace legacy = f15::math::legacy;
    const legacy::Math math(g_angleLut);
    const f15::math::EulerAngles<f15::math::GameBackend> angles{g_ourHead, g_ourPitch, g_ourRoll};
    g_orientMatrix = math.rotation(angles);
    legacy::storeTerms(math.terms(angles), g_rotSinYaw, g_rotCosYaw,
                       g_sphereRadius, g_sphereDistZ, g_spherePitch, g_sphereRoll);
    g_orientationDirty = 0;
    g_rotationCounter = 0;
}

uint16 signedRatio16(int16 numerator, int16 denominator) { /* Original: IntDiv(A,B). Divide two signed 15-bit fractions. */
    /* Plain char is unsigned on Android ARM; sign factors must preserve -1. */
    int numeratorSign = 1;
    int denominatorSign = 1;
    int32 absNumerator;
    int32 absDenominator;

    /* Divide two signed 15-bit fractions, then restore the combined sign. */
    if (numerator < 0) numeratorSign = -1;
    if (denominator < 0) denominatorSign = -1;
    absNumerator = (int32)(numerator < 0 ? -numerator : numerator);
    absDenominator = (int32)(denominator < 0 ? -denominator : denominator);
    /* Original callers consume the 16-bit quotient word, including wraparound
     * after applying the sign. Keep the DOS word pattern instead of returning a
     * host-width signed arithmetic result. */
    return (uint16)((uint16)((((uint32)(uint16)absNumerator) << 16) / (uint32)absDenominator >> 1) *
                    (int)numeratorSign * (int)denominatorSign);
done:;
}

#define ASIN_TABLE_SHIFT 9
#define WORD_DEGREE_STEP 256

int valueToAngle(int value) { /* Original: Iasin(A). Return 16-bit word-degree arcsin by table interpolation. */
    int angle, magnitude, tableIndex, tableSpan;

    if (value == (int)0x8000) return (int)0xc000;
    magnitude = abs(value);
    tableIndex = (magnitude >> ASIN_TABLE_SHIFT) + 1;
    for (; tableIndex >= 0; tableIndex--) {
        if (g_angleLut[tableIndex] <= magnitude) {
            tableSpan = g_angleLut[tableIndex + 1] - g_angleLut[tableIndex];
            angle = (int)((long)(magnitude - g_angleLut[tableIndex]) * WORD_DEGREE_STEP / (long)tableSpan) + tableIndex * WORD_DEGREE_STEP;
            break;
        }
    }
    if (value < 0) {
        angle = -angle;
    }
    return angle;
}

int complementAngle(int value) { /* Original: Iacos(A). Return 16-bit word-degree arccos as quarter-turn minus arcsin. */
    enum { WORD_DEGREES_QUARTER_TURN = 0x4000 };
    return WORD_DEGREES_QUARTER_TURN - valueToAngle(value);
}

int16 isqrt(int16 value) { /* Original: Sqrt(N). Return integer square root using Newton iteration. */
    int16 quotient, guess;
    /* Integer square root using Newton iteration seeded from value >> 2. */
    value = abs16Compat(value);
    if (value < 4) {
        return 1;
    }
    guess = value >> 2;
    do {
        quotient = value / guess;
        guess = (guess + quotient) >> 1;
    } while (abs16Compat(guess - quotient) > 1);
    return guess;
}

/* Q12 shortest-arc tween that snaps across the 0x8000 gimbal flip (see lerpAngle
 * in egsys.c) rather than sweeping the view 180deg for one frame. */
static int lerpViewAngle(int a0, int a1, int alpha) {
    int16 d = (int16)(a1 - a0);
    if (d >= 0x4000 || d <= -0x4000)
        return a1;
    return a0 + ((d * alpha) >> 12);
}

/* A gimbal flip rewrites the whole head/pitch/roll triple (see lerpPose in
 * egsys.c): if any component snaps, take the full new pose instead of mixing a
 * snapped component with still-tweening ones. */
static int poseSnapsQ12(const struct ViewSnapshot *s0, const struct ViewSnapshot *s1) {
    int16 dh = (int16)(s1->heading - s0->heading);
    int16 dp = (int16)(s1->pitch - s0->pitch);
    int16 dr = (int16)(s1->roll - s0->roll);
    return dh >= 0x4000 || dh <= -0x4000 || dp >= 0x4000 || dp <= -0x4000 ||
           dr >= 0x4000 || dr <= -0x4000;
}

/* split a Q8 fixed-point eye coordinate into the integer part + [0,255] frac */
static int32 eyeFromQ8(long q8, int16 *frac) {
    *frac = (int16)(q8 & 0xff);
    return (int32)(q8 >> 8);
}

void computeTrackingCameraAngles(int32 targetX, int32 targetY, int16 targetAlt,
                                 int32 viewX, int32 viewY, int16 viewAlt,
                                 int16 *heading, int16 *pitch) {
    enum { WORLD_Y_EXTENT = 0x100000 };
    int32 dx = targetX - viewX;
    /* Target Y is a world coordinate, while viewY is the renderer's inverted
     * coordinate. Convert them to the same space before taking the delta. */
    int32 dy = targetY - (WORLD_Y_EXTENT - viewY);
    int32 range = rangeApprox32(dx, dy);

    /* Fine world deltas exceed int16 on normal maps. computeBearing32 scales
     * both components equally, preserving the angle without truncation. */
    *heading = computeBearing32(dx, -dy);
    *pitch = -computeBearing32((int32)targetAlt - viewAlt, range);
}

// something to do with view switching?
void renderFrame() {
    int16 camDist, savedCamDist, camOffset, tmp;
    g_camEyeX = g_viewTargetX = fineUnits(g_ViewX);
    g_camEyeY = fineUnits(g_ViewY);
    g_viewTargetY = 0x100000 - fineUnits(g_ViewY);
    g_camEyeZ = g_viewZ + 0x18;
    g_camEyeFracX = g_camEyeFracY = g_camEyeFracZ = 0;
    g_viewTargetAlt = g_viewZ;
    camDist = g_externalCamDist = clampRange(g_externalCamDist, 2, 8);
    switch (g_viewMode) {
    case VIEW_COCKPIT:
    case VIEW_FORWARD:
        g_viewHeading = signedAngle(g_ourHead);
        g_viewPitch = signedAngle(g_ourPitch);
        g_viewRoll = signedAngle(g_ourRoll);
        break;
    case VIEW_REAR:
        g_viewHeading = signedAngle(g_ourHead) + 0x8000;
        g_viewPitch = -signedAngle(g_ourPitch);
        g_viewRoll = -signedAngle(g_ourRoll);
        break;
    case VIEW_RIGHT:
        g_viewHeading = signedAngle(g_ourHead) + 0x4000;
        g_viewPitch = -signedAngle(g_ourRoll);
        g_viewRoll = signedAngle(g_ourPitch);
        break;
    case VIEW_LEFT:
        g_viewHeading = signedAngle(g_ourHead) - 0x4000;
        g_viewPitch = signedAngle(g_ourRoll);
        g_viewRoll = -signedAngle(g_ourPitch);
        break;
    case VIEW_EXT_DYNAMIC: {
        /* The trailing replay camera reads a pose from the per-sim-step history
         * ring; interpolate between the two samples bracketing the delayed view
         * time (render alpha, Q12) so it tracks smoothly instead of stepping at
         * the sim rate. */
        int r1, a = g_renderAlphaQ12;
        struct ViewSnapshot *s0, *s1;
        tmp = (frameTick - ((g_frameRateScaling + 1) / 2) - 1) & 0xf;
        r1 = (tmp + 1) & 0xf;
        s0 = &g_viewSnapshotRing[tmp];
        s1 = &g_viewSnapshotRing[r1];
        if (poseSnapsQ12(s0, s1)) {
            g_viewHeading = s1->heading;
            g_viewPitch = s1->pitch;
            g_viewRoll = s1->roll;
        } else {
            g_viewHeading = lerpViewAngle(s0->heading, s1->heading, a);
            g_viewPitch = lerpViewAngle(s0->pitch, s1->pitch, a);
            g_viewRoll = lerpViewAngle(s0->roll, s1->roll, a);
        }
        g_camEyeX = s0->worldX + (int32)(((int64)(s1->worldX - s0->worldX) * a) >> 12);
        g_camEyeY = s0->worldY + (int32)(((int64)(s1->worldY - s0->worldY) * a) >> 12);
        g_camEyeZ = s0->alt + (((int32)(s1->alt - s0->alt) * a) >> 12);
        break;
    }
    case VIEW_EXT_SIDE:
        g_viewHeading = signedAngle(g_ourHead) - 0x4000;
        g_viewPitch = 0;
        g_viewRoll = 0;
        /* Q8 eye: a whole-fine-unit eye position lurches visibly at close cam
         * distance as the offset rotates with the (smoothly interpolated) heading. */
        g_camEyeX = eyeFromQ8(sinMulQ8(signedAngle(g_ourHead) + 0x4000, 0x18 << camDist) + ((long)fineUnits(g_ViewX) << 8), &g_camEyeFracX);
        g_camEyeY = eyeFromQ8(cosMulQ8(signedAngle(g_ourHead) + 0x4000, 0x18 << camDist) + ((long)fineUnits(g_ViewY) << 8), &g_camEyeFracY);
        break;
    case VIEW_EXT_UNUSED:
        g_viewHeading = 0x8000;
        g_viewPitch = 0;
        g_viewRoll = 0;
        g_camEyeY = (0x18 << camDist) + fineUnits(g_ViewY);
        break;
    case VIEW_EXT_FOLLOW:
        g_viewHeading = signedAngle(g_ourHead);
        g_viewPitch = 0;
        g_viewRoll = 0;
        g_camEyeX = eyeFromQ8(sinMulQ8(signedAngle(g_ourHead) + 0x8000, 0x18 << camDist) + ((long)fineUnits(g_ViewX) << 8), &g_camEyeFracX);
        g_camEyeY = eyeFromQ8(cosMulQ8(signedAngle(g_ourHead) + 0x8000, 0x18 << camDist) + ((long)fineUnits(g_ViewY) << 8), &g_camEyeFracY);
        g_camEyeZ = (4 << camDist) + g_viewZ;
        break;
    case VIEW_EXT_TARGET:
    case VIEW_MISSILE:
    case VIEW_TARGET:
        if (g_viewMode != VIEW_MISSILE) {
            if (g_currentWeaponType == 1) {
                // XXX: test byte ptr g_airTargetLock, 80h -> check which byte is tested, other byte ptr instructions in this routine
                if (!(g_airTargetLock & 0x80)) g_viewTargetObj = g_airTargetLock + 0x20;
            } else {
                if (!(g_groundTargetLock & 0x80)) g_viewTargetObj = g_groundTargetLock + 0x40;
            }
        } else {
            if (g_directorMode == 0) g_viewTargetObj = g_lastMissileSlot;
        }
        savedCamDist = camDist;
        if (!(g_viewTargetObj & 0x40)) {
            if (!(g_viewTargetObj & 0x20)) {
                if (g_projectiles[g_viewTargetObj].ttl != 0) {
                    /* Fine (sub-mapX-unit) interpolated position so the tracking
                     * camera doesn't lurch in 32-unit steps (the "earthquake"). */
                    g_viewTargetX = (uint32)g_projInterpX[g_viewTargetObj];
                    g_viewTargetY = (uint32)g_projInterpY[g_viewTargetObj];
                    g_viewTargetAlt = g_projectiles[g_viewTargetObj].alt;
                } else {
                    g_projectiles[g_viewTargetObj].worldX = signedAngle(g_ourHead);
                    g_projectiles[g_viewTargetObj].worldY = signedAngle(g_ourPitch);
                    if (g_directorMode != 0) g_viewMode = VIEW_EXT_FOLLOW;
                }
                camDist = 5;
            } else {
                // .... g_viewTargetObj & 0x1f
                g_viewTargetX = g_simObjects[g_viewTargetObj & 0x1f].worldX;
                g_viewTargetY = g_simObjects[g_viewTargetObj & 0x1f].worldY;
                g_viewTargetAlt = g_simObjects[g_viewTargetObj & 0x1f].alt;
                camDist = 5;
            }
        } else {
            g_viewTargetX = (uint32)g_planeTable.planes[g_viewTargetObj & 0x3f].mapX << 5;
            g_viewTargetY = (uint32)g_planeTable.planes[g_viewTargetObj & 0x3f].mapY << 5;
            g_viewTargetAlt = g_planeTable.planes[g_viewTargetObj & 0x3f].flags & 0x200 ? 200 : 50;
            camDist = 7;
            if (g_autopilotEngaged != 0 && g_directorEventDeadline == -1) camDist = 6;
        }
        if (g_directorMode == 0) camDist = savedCamDist;
        computeTrackingCameraAngles((int32)g_viewTargetX, (int32)g_viewTargetY,
                                    g_viewTargetAlt, fineUnits(g_ViewX), fineUnits(g_ViewY), g_viewZ,
                                    &g_viewHeading, &g_viewPitch);
        g_viewRoll = 0;
        camOffset = cosMul(g_viewPitch, 0x18 << camDist);
        if (g_viewTargetObj & 0x60 || g_directorMode != 0) {
            if (g_viewMode == VIEW_EXT_TARGET) {
                g_camEyeX = eyeFromQ8(sinMulQ8(g_viewHeading + 0x8000, camOffset) + ((long)fineUnits(g_ViewX) << 8), &g_camEyeFracX);
                g_camEyeY = eyeFromQ8(cosMulQ8(g_viewHeading + 0x8000, camOffset) + ((long)fineUnits(g_ViewY) << 8), &g_camEyeFracY);
                g_camEyeZ = (int16)eyeFromQ8(sinMulQ8(g_viewPitch, 0x18 << camDist) + ((long)((4 << camDist) + g_viewZ) << 8), &g_camEyeFracZ);
                g_viewPitch = -g_viewPitch;
            } else {
                g_camEyeX = eyeFromQ8(sinMulQ8(g_viewHeading, camOffset) + ((long)g_viewTargetX << 8), &g_camEyeFracX);
                g_camEyeY = eyeFromQ8(cosMulQ8(g_viewHeading, camOffset) + (((long)0x100000 - (long)g_viewTargetY) << 8), &g_camEyeFracY);
                g_camEyeZ = (int16)eyeFromQ8((((long)(4 << camDist) + g_viewTargetAlt) << 8) - sinMulQ8(g_viewPitch, 0x18 << camDist), &g_camEyeFracZ);
                if (g_viewTargetObj & 0x40 && g_planeTable.planes[g_viewTargetObj & 0x3f].flags & 0x200 && g_camEyeZ < 132) {
                    g_camEyeZ = 132;
                    g_camEyeFracZ = 0;
                }
                g_viewHeading += 0x8000;
            }
        } else {
            g_viewHeading = g_projectiles[g_viewTargetObj].worldX;
            g_viewPitch = g_projectiles[g_viewTargetObj].worldY - 0x400;
            camOffset = cosMul(g_viewPitch, 0x10 << camDist);
            g_camEyeX = eyeFromQ8(((long)g_viewTargetX << 8) - sinMulQ8(g_viewHeading, camOffset), &g_camEyeFracX);
            g_camEyeY = eyeFromQ8(((long)0x100000 << 8) - (cosMulQ8(g_viewHeading, camOffset) + ((long)g_viewTargetY << 8)), &g_camEyeFracY);
            g_camEyeZ = (int16)eyeFromQ8(((long)g_viewTargetAlt << 8) - sinMulQ8(g_viewPitch, 0x10 << camDist), &g_camEyeFracZ);
        }
        break;
    case VIEW_EJECT:
        g_viewPitch = 0xf400;
        g_viewRoll = 0;
        g_camEyeX = (int32)g_crashCamX << 5;
        g_camEyeY = (0x8000 - (int32)g_crashCamY) << 5;
        g_camEyeZ = g_crashCamZ;
        break;
    default:
        break;
    }
    /* barrel roll */
    if (abs(g_viewPitch) > 0x4000 || g_viewPitch == INT16_MIN) {
        g_viewPitch = INT16_MIN - g_viewPitch;
        g_viewHeading += INT16_MIN;
        g_viewRoll = INT16_MIN - g_viewRoll;
    }
    /* Build the reticle/box rotation from the (interpolated) view angles every
     * render frame — the very g_viewHeading/Pitch/Roll render3DView projects the
     * world from — so the HUD boxes track the world smoothly as the player
     * maneuvers. The forward view (g_viewMode 0) formerly copied the sim-rate
     * g_orientMatrix here; g_orientMatrix is itself buildRotationMatrixFar of the
     * player angles (rebuildOrientation), so this is the same matrix when static,
     * but under the render/sim decouple the copy only updated the boxes at the sim
     * rate while the world turned every render frame. */
    buildRotationMatrixFar(g_camRotMatrix, g_viewHeading, g_viewPitch, g_viewRoll);
    if (g_camEyeZ < 0x10) {
        g_camEyeZ = 0x10;
        g_camEyeFracZ = 0;
    }
    tmp = g_hudVisible;
    g_hudVisible = (g_viewMode & 0xc0) == 0;
    if (tmp != g_hudVisible) {
        gfx_waitRetrace();
        if (g_hudVisible != 0) {
            gfx_nop23();
            gfx_restoreFromImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
            UpdateThrottleState();
            drawWeaponAmmo();
            drawWeaponSelectMarker(missileSpecIndex);
            if (g_mapMode == 0) {
                redrawTacMap(g_viewX_, g_viewY_);
            }
            g_groundTargetLock = g_airTargetLock = 0xffff;
            fillPanelBox(3, 3);
            g_lockedTargetKilled = 0;
        } else {
            gfx_captureToImage(g_eg2dBacking, *g_pageFront, 0, 97, 0, 97, 320, 103);
        }
    }
    if (g_viewMode != g_lastViewKey) {
        if (g_viewMode == VIEW_LEFT || g_viewMode == VIEW_RIGHT || g_viewMode == VIEW_REAR) {
            gfx_waitRetrace();
            if (gfx_getModecode() == 3) {
                openBlitClosePic(g_viewMode == VIEW_LEFT ? "256Left.Pic" : g_viewMode == VIEW_RIGHT ? "256Right.Pic"
                                                                                     : "256Rear.Pic",
                                 *g_pageFront);
            } else {
                openBlitClosePic(g_viewMode == VIEW_LEFT ? "Left.Pic" : g_viewMode == VIEW_RIGHT ? "Right.Pic"
                                                                                  : "Rear.Pic",
                                 *g_pageFront);
            }
            g_pageFront[8] = 96;
        } else {
            g_pageFront[8] = g_hudVisible != 0 ? 96 : 199;
        }
        g_lastViewKey = g_viewMode;
    }
    g_horizonGroundColor = g_world3dData[47];
    *(uint8 *)(&g_skyColorIndex) = 3;
    if (g_detailLevel == 0) {
        g_horizonGroundColor = 3;
        *(uint8 *)(&g_skyColorIndex) = 0x0b;
    }
    loadColorPalette(g_nightMode);
    *(uint8 *)(&g_posVisibleFlag) = 0;
    render3DView(-g_viewHeading, g_viewPitch, g_viewRoll, g_camEyeX, g_camEyeY, (int32)g_camEyeZ, 0, 0, 320, g_pageFront[8] + 1);
    g_extraScaleShift = 0;
    g_savedPosVisible = g_posVisibleFlag;
    if (g_viewMode == VIEW_REAR && !gfx_hasPageReplacement()) {
        // draw vertical stabilizers in rear view
        drawVectorShape(g_rearViewShape);
        gfx_setColor(0xf);
        g_lineX1 = 241;
        g_lineY1 = 21;
        g_lineX2 = 251;
        g_lineY2 = 94;
        drawClipLineGlobal();
        g_lineX1 = 83;
        g_lineY1 = 21;
        g_lineX2 = 73;
        g_lineY2 = 94;
        drawClipLineGlobal();
        gfx_nop23();
        // top of the helmet
        blitSprite(107, 48, 209, 0, 111, 47, 0);
        blitSprite(65, 95, 125, 54, 195, 2, 0);
    }
    g_hudBottomY = (g_activePanelMode == 0x13 || g_mapMode == 1 || g_hudVisible == 0) ? 200 : 97;

    /* GL (native overlay) rebuilds the 2D every present, so the tac map — cached
     * into g_eg2dBacking for the software page — must be re-emitted into this frame's
     * native stream, crisp at window resolution. Runs inside the vector frame
     * (render3DView opened it above). It reloads colorLut to the map palette;
     * save/restore it so the HUD/gauges drawn after keep the flight palette
     * render3DView set. The software (retained page) path leaves the cached map alone. */
    if (r2d_vectorActive()) {
        uint8 savedLut[16];
        memcpy(savedLut, colorLut, 16);
        renderTacMapOverlay();
        memcpy(colorLut, savedLut, 16);
    }
}

void UpdateThrottleState(void) {
    if (g_hudVisible != 0) {
        setDrawColor(COLOR_BLACK);
        fillRectBoth(212, 127, 222, 175);
        setDrawColor(COLOR_LIGHTRED);
        fillRectBoth(212, -(g_setThrust / 3 - 175), 222, 175);
        if (100 < g_setThrust) {
            setDrawColor(COLOR_YELLOW);
            fillRectBoth(212, -(g_setThrust / 3 - 175), 222, 142);
        }
    }
}
void drawFuelGauge(void) {
    if (g_hudVisible == 0) {
        return;
    }
    setDrawColor(COLOR_BLACK);
    fillRectBoth(5, 109, 10, 152);
    setDrawColor(g_fuelRemaining > 2000 ? COLOR_GREEN : COLOR_YELLOW);
    fillRectBoth(5, -(g_fuelRemaining / 250 - 152), 10, 152);
}

void drawVectorShape(const int16 *shapeData) {
    while (*shapeData != -1) {
        int color = colorLut[*shapeData++];
        gfx_setColor(color);
        if (r2d_vectorActive()) {
            /* The shape draws inside the 3D viewport's show-through rect, so on GL a
             * span fill baked into the page would be composited away. Submit the
             * sub-shape's vertex ring as an immediate native-res filled polygon (its
             * slanted highlight edge is drawn separately via gfx_drawLine → native). */
            short ring[64 * 2];
            int nv = 0;
            while (*shapeData != -1 && nv < 64) {
                ring[nv * 2] = *shapeData++;
                ring[nv * 2 + 1] = *shapeData++;
                nv++;
            }
            /* The list repeats the first vertex to close the loop; GL_POLYGON closes
             * implicitly, so drop the trailing duplicate. */
            if (nv >= 2 && ring[0] == ring[(nv - 1) * 2] && ring[1] == ring[(nv - 1) * 2 + 1])
                nv--;
            /* Extend the bottom edge one pixel down so the GL fill closes the seam to
             * the body sprite below (the span fill covers it on software). */
            {
                int k, maxY = ring[1];
                for (k = 1; k < nv; k++)
                    if (ring[k * 2 + 1] > maxY) maxY = ring[k * 2 + 1];
                for (k = 0; k < nv; k++)
                    if (ring[k * 2 + 1] == maxY) ring[k * 2 + 1] = maxY + 1;
            }
            r2d_submitPoly(ring, nv, color, 0, 0, 320, 200);
        } else {
            resetScanlineSpans();
            shapeData += 2;
            while (*shapeData != -1) {
                g_lineX1 = shapeData[-2];
                g_lineY1 = shapeData[-1];
                g_lineX2 = *shapeData++;
                g_lineY2 = *shapeData++;
                clipAndRasterizeEdge();
            }
            flushSpanDirtyRect();
        }
        shapeData++;
    }
}

void waitForKeyPress(void) {
    int16 savedTiming;
    int key;

    audio_engineDroneOff();
    savedTiming = g_frameTimingAccum;
    /* The DOS version busy-polled BIOS kbhit(). The native blocking reader
     * pumps SDL/blackbox input and yields between polls, keeping pause from
     * consuming a CPU core while preserving Alt+P's wait-for-another-key rule. */
    do {
        key = egReadKey();
    } while (key == SCAN_ALT_P);
    updateEngineSound();
    g_frameTimingAccum = savedTiming;
}
