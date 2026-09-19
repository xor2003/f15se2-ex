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
    for (bool fuelTick : {false, true}) {
        g_initPhase = 1;
        g_frameRateScaling = hz;
        frameTick = fuelTick ? hz * 2 : 1;
        g_thrust = legacy::thrustFromUnits(initial);
        g_setThrust = requested;
        g_fuelRemaining = fuel;
        g_gunHits = damage;
        g_hudVisible = g_inputDisabled = 0;
        g_autopilotAltitude = g_autopilotEngaged = 0;
        g_ejectState = g_autoCrashDive = g_currentWeaponType = 0;
        g_groundAltitude = 0;
        const int sceneHeight = height < 8192 ? height : height < 16384 ?
            (height - 8192) / 2 + 8192 : (height - 16384) / 4 + 12288;
        g_viewZ = sceneHeight;
        g_altitude = legacy::altitudeFromUnits(height);
        g_velocity = legacy::speedFromUnits(8100);
        g_knots = 300;
        g_playerPlaneFlags = (gearUp ? 1 : 0) | (airBrake ? 8 : 0);
        g_gearDownArmed = 0;
        g_cornerSpeed = 100;
        g_kbdSensitivity = 2;
        require(SDL_SetJoystickVirtualAxis(stick, 1,
            stickPitch == 1 ? -32768 : stickPitch == 254 ? 32767 : 0), "set pitch axis");
        SDL_UpdateJoysticks();
        input_pumpEvents();
        require(input_preferGamepad(), "flight selects virtual joystick");
        g_ViewX = legacy::viewX(0);
        g_ViewY = legacy::viewY(0);
        g_ourHead = {};
        g_ourPitch = legacy::angleFromWord(pitch);
        g_ourRoll = legacy::angleFromWord(roll);
        g_stallSpeed = {};
        g_liftForce = g_rollPitchTrim = {};
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
        int pitchCommand = (stickPitch / 16) - 8;
        if (pitchCommand < 0) ++pitchCommand;
        pitchCommand *= 6;
        if (pitchCommand < 0) pitchCommand /= 2;
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
        const int yawStep = yawRate / hz;
        auto expectedMatrix = rotation_reference::rotation(0, pitch, roll, g_angleLut);
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
        require(joyAxes[0] == 128 && joyAxes[1] == stickPitch, "flight input reaches requested position");
        require(legacy::thrustUnits(g_thrust) == expected, "full flight model thrust response changed");
        require(g_fuelRemaining == remaining, "full flight model fuel cadence/depletion changed");
        require(g_setThrust == target, "damage thrust limit changed");
        const int actualLoad = legacy::loadSixteenths(g_gees);
        if (actualLoad != gees)
            std::fprintf(stderr, "hz=%d height=%d roll=%d stick=%d raw=%d pitch=%d load=%d expected=%d\n",
                hz, height, roll, stickPitch, int(g_joyRawY), legacy::pitchInput(g_pitchInput), actualLoad, gees);
        require(actualLoad == gees, "bank and pitch-command load changed");
        require(legacy::pitchInput(g_pitchInput) == pitchCommand, "load cap pitch-command reduction changed");
        require(g_cornerSpeed == corner && AirspeedBoundary<FixedBackend>::stall(g_stallSpeed) == corner * 27,
                "full flight model corner/stall threshold changed");
        require(legacy::speedUnits(g_velocity) == velocity && g_knots == velocity / 27,
                "full flight model target-speed/acceleration order changed");
        require(legacy::signedAngle(g_liftForce) == lift && legacy::signedAngle(g_rollPitchTrim) == trim,
                "lift must sample accelerated speed before braking and the initial roll");
    }
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
