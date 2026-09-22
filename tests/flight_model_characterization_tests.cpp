#include "math/legacy_airspeed.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_propulsion.hpp"
#include "math/legacy_flight_control.hpp"
#include "math_rotation_reference.hpp"
#include "egdata.h"
#include "egflight.h"
#include "comm.h"
#include "joystick.h"
#include "input.h"
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

void stepFlightModel();
void rebuildOrientation();

namespace {
using namespace f15::math;
void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
// Frozen Newton approximation: new non-neutral loads include results that are
// not floor(sqrt(value)), such as 24 -> 5. Do not call migrated math here.
int cornerRoot(int value) {
    if (value < 4) return 1;
    int guess = value / 4, quotient;
    do {
        quotient = value / guess;
        guess = (guess + quotient) / 2;
    } while (std::abs(guess - quotient) > 1);
    return guess;
}

int recoveryBearing(int x, int y) {
    using rotation_reference::word;
    x = word(x);
    y = word(y);
    if (!x) return word(y > 0 ? 0 : 32768);
    if (!y) return word(x > 0 ? 16384 : 49152);
    const bool swapped = std::abs(x) > std::abs(y);
    const int ratio = std::min(std::abs(x), std::abs(y)) * 16384 /
                      std::max(std::abs(x), std::abs(y));
    const int angle = (10240 - std::abs(4915 - ratio) * 2816 / 16384) * ratio / 16384;
    if (x > 0) return word(y > 0 ? (swapped ? 16384 - angle : angle) :
                                              (swapped ? angle + 16384 : 32768 - angle));
    return word(y > 0 ? (swapped ? angle + 49152 : -angle) :
                       (swapped ? 49152 - angle : angle + 32768));
}

void recoveryGuidance(SDL_Joystick *stick) {
    using rotation_reference::word;
    using rotation_reference::floorDivide;
    const auto clamp = [](int value, int lo, int hi) {
        value = word(value);
        return value > hi ? hi : value >= lo ? value : value <= -16384 ? hi : lo;
    };
    require(SDL_SetJoystickVirtualAxis(stick, 1, 0), "neutral recovery stick");
    SDL_UpdateJoysticks();
    input_pumpEvents();
    for (bool carrier : {false, true})
    for (int direction : {-1, 1})
    for (int corridor : {0, 1, 2})
    for (int heading : {0, 511, 512, 16384, 16385, -32768})
    for (int x : {-16000, -128, 0, 128, 16000})
    for (int y : {-16000, -256, 0, 256, 16000})
    for (int knots : {160, 349, 350, 800})
    for (int roll : {-8192, 0, 8192})
    for (int hz : {4, 15}) {
        g_initPhase = 1;
        g_frameRateScaling = f15::math::SimRate::fromWord(hz);
        frameTick = f15::math::Ticks::fromWord(1);
        g_thrust = legacy::thrustFromUnits(35);
        g_setThrust = 5;
        g_fuelRemaining = 5000;
        g_gunHits = g_hudVisible = g_inputDisabled = 0;
        g_ejectState = g_autoCrashDive = g_currentWeaponType = 0;
        g_groundAltitude = 0;
        g_viewZ = 3000;
        g_autopilotAltitude = f15::math::legacy::renderHeightFromUnits(3000);
        g_autopilotEngaged = 0;
        g_waypointBearing = legacy::angleFromWord(0);
        g_missionTick = f15::math::TickDuration{};
        waypointIndex = 3;
        g_targetSlots[1].viewIndex = 1;
        g_planeTable.planes[1].mapX = 10000 + x;
        g_planeTable.planes[1].mapY = 10000 + y;
        g_planeTable.planes[1].flags = carrier ? 0x200 : 0;
        g_viewX_ = g_viewY_ = 10000;
        g_northSouthSign = direction;
        g_inLandingCorridor = corridor != 0;
        g_closestThreatIndex = corridor == 2 ? 2 : 1;
        g_slowMotionMode = 2;
        g_altitude = legacy::altitudeFromUnits(3000);
        g_velocity = legacy::speedFromUnits(8100);
        g_knots = AirspeedMath<GameBackend>::knots(knots);
        g_playerPlaneFlags = 1;
        g_gearDownArmed = 0;
        g_cornerSpeed = AirspeedMath<GameBackend>::knots(100);
        g_kbdSensitivity = 2;
        g_ViewX = legacy::viewX(0);
        g_ViewY = legacy::viewY(0);
        g_ourHead = legacy::angleFromWord(heading);
        g_ourPitch = {};
        g_ourRoll = legacy::angleFromWord(roll);
        g_stallSpeed = {};
        g_liftForce = g_rollPitchTrim = {};
        g_orientationDirty = g_rotationCounter = g_rollWasNonzero = 0;
        rebuildOrientation();

        const int ns = carrier ? direction : (y > 0 ? -1 : y < 0 ? 1 : 0);
        int dx = x, dy = y + (carrier ? 30 : 64) * ns;
        int error = word(std::abs(heading));
        if (ns == -1) {
            dx = -dx; dy = -dy;
            error = word(std::abs(int(word(heading - 32768))));
        }
        int height = clamp((std::abs(dx) + std::abs(dy)) * 2 + error / 32, 50, 4096);
        const int slow = height < 4096 ? 1 : 2;
        if (carrier) height += 100;
        if (corridor == 1 && std::abs(error) < 512) height = -20;
        dy = 10000 + y + (carrier ? 28 : 56) * ns + clamp(std::abs(dx) * 4 + error / 16, 0, 3072) * ns;
        bool brake = false;
        if (error > 16384) { dx = 10000 + x; height = 4096; }
        else { dx = word(10000 + x + ns * dx * 2); brake = 400 < knots; }
        const int limit = knots / 16 * 256;
        error = word(std::clamp(int(word(recoveryBearing(dx - 10000, 10000 - dy) - heading)), -limit, limit) * 2);
        if (corridor == 1) error = 0;
        const int rollCommand = -clamp(floorDivide(word(error - roll), 64), -32, 32);
        const int throttle = clamp(std::abs(error) / 256 + height / 64, 35, 80);
        const int pitchCommand = clamp(std::clamp(int(floorDivide(height - 3000, 8)), -24, 24), -16, 16);
        stepFlightModel();
        require(legacy::rollInput(g_rollInput) == rollCommand, "recovery roll changed");
        require(legacy::pitchInput(g_pitchInput) == pitchCommand, "recovery pitch changed");
        require(g_setThrust == throttle, "recovery throttle changed");
        require((g_playerPlaneFlags & 9) == ((knots >= 350 ? 1 : 0) | (brake ? 8 : 0)), "recovery gear/brakes changed");
        require(g_slowMotionMode == slow, "recovery slow-motion transition changed");
    }
}

// Characterize dbfb4ab before migrating thrust and target-speed generation.
// Non-neutral input/load coverage added against 9371b5b before load migration.
// No flight-model or math functions are replaced with test doubles.
void thrustAndFuel() {
    const auto config = std::filesystem::temp_directory_path() /
        ("f15-flight-characterization-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(config);
    SDL_setenv_unsafe("F15_JOY_CONFIG_DIR", config.string().c_str(), 1);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    require(SDL_Init(SDL_INIT_GAMEPAD), "initialize virtual flight input");
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
    desc.naxes = 2;
    desc.name = "Flight characterization";
    const auto id = SDL_AttachVirtualJoystick(&desc);
    require(id != 0, "attach flight joystick");
    auto *stick = SDL_OpenJoystick(id);
    require(stick != nullptr, "open flight joystick");
    SDL_Event added{};
    added.type = SDL_EVENT_JOYSTICK_ADDED;
    added.jdevice.which = id;
    joy_handleEvent(&added);
    joy_init();
    input_setMode(INPUT_MODE_FLIGHT);
    input_setJoystickSetup(false);
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    require(SDL_PushEvent(&focus), "focus flight input");
    input_pumpEvents();
    Game game{};
    GameComm comm{};
    gameData = &game;
    commData = &comm;
    game.unk4 = 2;
    for (int hz : {4, 15})
    for (int initial : {0, 1, 35, 100, 144})
    for (int requested : {0, 1, 35, 100, 144})
    for (int damage : {0, 12, 40})
    for (int fuel : {0, 1, 5000})
    for (int height : {2000, 4095, 8192, 16383, 16384, 60000})
    for (int pitch : {-4096, 0, 4096})
    for (int roll : {-8192, 0, 8192, 12288, 16384, 24576})
    for (int stickPitch : {1, 128, 254})
    for (bool gearUp : {false, true})
    for (bool airBrake : {false, true})
    for (bool fuelTick : {false, true})
    for (int autopilotCase : {0, 1, 2, 3, 4})
    for (bool disabled : {false, true})
    for (int heading : {0, 1, -1, 32767, -32768}) {
        if (!autopilotCase && heading != 0) continue;
        // Freeze the disabled-input policy before separating it from device
        // normalization. The original zero byte is full deflection, not center.
        if (disabled && (autopilotCase || initial != 100 || requested != 100 ||
            damage || fuel != 5000 || height != 2000 || !gearUp || airBrake || fuelTick)) continue;
        // Add a focused autopilot grid without multiplying unrelated fuel and
        // engine cases. Neutral stick is required for altitude hold to survive.
        if (autopilotCase && (initial != 100 || requested != 100 || damage != 0 ||
            fuel != 5000 || stickPitch != 128 || !gearUp || airBrake || fuelTick)) continue;
        g_initPhase = 1;
        g_frameRateScaling = f15::math::SimRate::fromWord(hz);
        frameTick = f15::math::Ticks::fromWord((int16)(fuelTick ? hz * 2 : 1));
        g_thrust = legacy::thrustFromUnits(initial);
        g_setThrust = requested;
        g_fuelRemaining = fuel;
        g_gunHits = damage;
        g_hudVisible = 0;
        g_inputDisabled = disabled;
        g_autopilotAltitude = {};
        g_autopilotEngaged = 0;
        g_ejectState = g_autoCrashDive = g_currentWeaponType = 0;
        g_groundAltitude = 0;
        const int sceneHeight = height < 8192 ? height : height < 16384 ?
            (height - 8192) / 2 + 8192 : (height - 16384) / 4 + 12288;
        const int altitudeTarget = autopilotCase == 1 ? sceneHeight + 1 :
            autopilotCase == 2 ? sceneHeight - 1 : autopilotCase == 3 ? 1 : 32767;
        const int bearingTarget = autopilotCase == 1 ? 1 : autopilotCase == 2 ? -1 :
            autopilotCase == 3 ? 32767 : -32768;
        const int initialTrim = !autopilotCase ? 0 : autopilotCase == 1 ? 1 :
            autopilotCase == 2 ? -1 : autopilotCase == 3 ? -32768 : 32767;
        waypointIndex = 0;
        g_missionTick = f15::math::TickDuration::fromWord(autopilotCase == 3 ? 0 : 15);
        g_waypointBearing = legacy::angleFromWord(bearingTarget);
        if (autopilotCase) {
            g_autopilotAltitude = f15::math::legacy::renderHeightFromUnits(altitudeTarget);
            g_autopilotEngaged = autopilotCase >= 3;
        }
        if (disabled) g_autopilotAltitude = f15::math::legacy::renderHeightFromUnits(altitudeTarget);
        g_viewZ = sceneHeight;
        g_altitude = legacy::altitudeFromUnits(height);
        g_velocity = legacy::speedFromUnits(8100);
        g_knots = AirspeedMath<GameBackend>::knots(300);
        g_playerPlaneFlags = (gearUp ? 1 : 0) | (airBrake ? 8 : 0);
        g_gearDownArmed = 0;
        g_cornerSpeed = AirspeedMath<GameBackend>::knots(100);
        g_kbdSensitivity = 2;
        require(SDL_SetJoystickVirtualAxis(stick, 1,
            stickPitch == 1 ? -32768 : stickPitch == 254 ? 32767 : 0), "set pitch axis");
        SDL_UpdateJoysticks();
        input_pumpEvents();
        require(input_preferGamepad(), "flight selects virtual joystick");
        g_ViewX = legacy::viewX(0);
        g_ViewY = legacy::viewY(0);
        g_ourHead = legacy::angleFromWord(heading);
        g_ourPitch = legacy::angleFromWord(pitch);
        g_ourRoll = legacy::angleFromWord(roll);
        g_stallSpeed = {};
        g_liftForce = g_rollPitchTrim = {};
        g_rollPitchTrim = legacy::angleFromWord(initialTrim);
        g_orientationDirty = g_rotationCounter = g_rollWasNonzero = 0;
        rebuildOrientation();
        // Preserve the pre-correction matrix output even if stall recovery
        // rebuilds g_orientMatrix later in the step.
        g_matrixScratch = g_orientMatrix;

        const int target = damage ? std::min(requested, std::max(0, 144 - damage * 4)) : requested;
        int expected = initial + ((target - initial) / 4) / hz;
        if (target > expected) ++expected;
        if (target < expected) expected = target;
        int remaining = fuel;
        if (fuelTick && target) remaining -= target * target / 750 + 2;
        if (remaining <= 0) { remaining = 0; expected = 0; }

        // Preserve each signed-word store and division separately. The oracle
        // uses the frozen LUT interpolator, never production math operations.
        using rotation_reference::floorDivide;
        using rotation_reference::word;
        const int expectedStickPitch = disabled ? 0 : stickPitch;
        int pitchCommand = (expectedStickPitch / 16) - 8;
        if (pitchCommand < 0) ++pitchCommand;
        pitchCommand *= 6;
        if (pitchCommand < 0) pitchCommand /= 2;
        int rollCommand = disabled ? 126 : 0;
        if (autopilotCase) {
            const int offset = autopilotCase >= 3 ? g_missionTick.phase(16) * 256 - 2048 : 0;
            const int headingError = std::clamp(int(word(offset - heading + bearingTarget)), -5120, 5120) * 2;
            rollCommand = -std::clamp(int(floorDivide(word(headingError - roll), 64)), -24, 24);
            const int altitudeError = std::clamp((altitudeTarget - sceneHeight) * 16 - initialTrim, -5120, 3072);
            pitchCommand = std::clamp(int(floorDivide(altitudeError - pitch, 128)), -8, 8);
        }
        const int bankLoad = g_rollGeeTable[(std::abs(roll) / 256) & 127];
        const int gees = std::min(128, bankLoad + pitchCommand / 2);
        if (bankLoad + pitchCommand / 2 > 128) {
            const int limit = 128 - bankLoad;
            // Preserve the original ordered clamp even when its upper bound
            // is negative: it is not equivalent to std::clamp/min(max()).
            pitchCommand = limit > pitchCommand ? pitchCommand : std::max(0, limit);
        }
        const int pitchDrag = word(floorDivide(
            std::int64_t(rotation_reference::sine(pitch, g_angleLut)) * 80 + 16384, 32768));
        int targetSpeed = word((expected - pitchDrag) * 800 / 100);
        targetSpeed = word(floorDivide((sceneHeight / 128 + 1024) * targetSpeed, 1024));
        targetSpeed = word(targetSpeed * (100 - remaining / 512) / 90);
        targetSpeed = word(floorDivide(targetSpeed * (128 - gees), 128));
        if (!gearUp) targetSpeed = word(targetSpeed - floorDivide(targetSpeed, 8));
        targetSpeed = std::clamp(targetSpeed, 0, 899) * 27;
        const int beforeBrakes = 8100 + ((targetSpeed - 8100) / 16) / hz;
        const int velocity = beforeBrakes - (airBrake ? (beforeBrakes / 16) / hz : 0);
        const int root = cornerRoot(gees * 4);
        const int corner = std::abs(word(root * word(100 * (height / 64 + 1024) / 1024) / 8));
        int lift = word(word(corner * 27) * 3072 / (std::abs(beforeBrakes) + 1));
        if (std::uint16_t(lift) > 8192) lift = 8192;
        const int trim = word(floorDivide(std::int64_t(word(lift - 768)) *
            rotation_reference::sine(roll + 16384, g_angleLut) + 16384, 32768));

        // Characterize aerodynamic yaw at 742431d before migrating it. The
        // old expression narrows after division and again after cosine scaling.
        const int bankTurn = word(floorDivide(
            std::int64_t(rotation_reference::sine(roll, g_angleLut)) * word(gees * 16) + 16384, 32768));
        const int dividedYaw = word(bankTurn * 128 / (word(floorDivide(velocity, 512)) + 32));
        const int yawRate = word(floorDivide(
            std::int64_t(rotation_reference::sine(pitch + 16384, g_angleLut)) * dividedYaw + 16384, 32768));
        const int pitchStep = word(pitchCommand * 128) / hz;
        const int rollStep = rollCommand * 128 / hz;
        const int yawStep = yawRate / hz;
        auto expectedMatrix = rotation_reference::rotation(heading, pitch, roll, g_angleLut);
        if (rollStep) {
            const int16 s = rotation_reference::sine(rollStep, g_angleLut);
            const int16 c = rotation_reference::sine(rollStep + 16384, g_angleLut);
            const rotation_reference::Matrix delta{c, s, 0, word(-s), c, 0, 0, 0, 32767};
            expectedMatrix = rotation_reference::multiply(expectedMatrix, delta);
        }
        if (pitchStep) {
            const int16 s = rotation_reference::sine(pitchStep, g_angleLut);
            const int16 c = rotation_reference::sine(pitchStep + 16384, g_angleLut);
            const rotation_reference::Matrix delta{32767, 0, 0, 0, c, word(-s), 0, s, c};
            expectedMatrix = rotation_reference::multiply(expectedMatrix, delta);
        }
        if (yawStep) {
            const int16 s = rotation_reference::sine(yawStep, g_angleLut);
            const int16 c = rotation_reference::sine(yawStep + 16384, g_angleLut);
            const rotation_reference::Matrix delta{c, 0, s, 0, 32767, 0, word(-s), 0, c};
            expectedMatrix = rotation_reference::multiply(delta, expectedMatrix);
        }

        stepFlightModel();
        rotation_reference::Matrix actualMatrix{};
        legacy::Codec::matrixWords(g_matrixScratch, actualMatrix.data());
        require(actualMatrix == expectedMatrix,
                "full flight model pitch/yaw matrix differs from frozen scalar formula");
        require(joyAxes[0] == (disabled ? 0 : 128) && joyAxes[1] == expectedStickPitch,
                "flight input reaches requested position or disabled sentinel");
        require(legacy::thrustUnits(g_thrust) == expected, "full flight model thrust response changed");
        require(g_fuelRemaining == remaining, "full flight model fuel cadence/depletion changed");
        require(g_setThrust == target, "damage thrust limit changed");
        const int actualLoad = legacy::loadSixteenths(g_gees);
        if (actualLoad != gees)
            std::fprintf(stderr, "hz=%d height=%d roll=%d stick=%d raw=%d pitch=%d load=%d expected=%d\n",
                hz, height, roll, stickPitch, int(g_joyRawY), legacy::pitchInput(g_pitchInput), actualLoad, gees);
        require(actualLoad == gees, "bank and pitch-command load changed");
        require(legacy::pitchInput(g_pitchInput) == pitchCommand, "load cap pitch-command reduction changed");
        require(legacy::rollInput(g_rollInput) == rollCommand, "altitude-hold roll command changed");
        if (autopilotCase)
            require(f15::math::legacy::Altitudes::render(g_autopilotAltitude) == altitudeTarget, "neutral input unexpectedly cancels altitude hold");
        if (disabled)
            require(g_autopilotAltitude.isZero(), "disabled zero-byte deflection must retain legacy altitude-hold cancellation");
        require(g_cornerSpeed == AirspeedMath<GameBackend>::knots(corner) && AirspeedBoundary<FixedBackend>::stall(g_stallSpeed) == corner * 27,
                "full flight model corner/stall threshold changed");
        require(legacy::speedUnits(g_velocity) == velocity && legacy::knotsUnits(g_knots) == velocity / 27,
                "full flight model target-speed/acceleration order changed");
        require(legacy::signedAngle(g_liftForce) == lift && legacy::signedAngle(g_rollPitchTrim) == trim,
                "lift must sample accelerated speed before braking and the initial roll");
    }
    recoveryGuidance(stick);
    gameData = nullptr;
    commData = nullptr;
    joy_shutdown();
    SDL_CloseJoystick(stick);
    require(SDL_DetachVirtualJoystick(id), "detach flight joystick");
    SDL_Quit();
    SDL_unsetenv_unsafe("F15_JOY_CONFIG_DIR");
    std::filesystem::remove_all(config);
}
}
int main() { thrustAndFuel(); }
