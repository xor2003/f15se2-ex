#include "math/legacy_airspeed.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_propulsion.hpp"
#include "math_rotation_reference.hpp"
#include "egdata.h"
#include "egflight.h"
#include "comm.h"
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

// Characterize dbfb4ab before migrating thrust and target-speed generation.
// No flight-model or math functions are replaced with test doubles.
void thrustAndFuel() {
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
    for (int height : {2000, 4095, 8192})
    for (int pitch : {-4096, 0, 4096})
    for (int roll : {-8192, 0, 8192, 12288, 16384, 24576})
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
        g_viewZ = height;
        g_altitude = legacy::altitudeFromUnits(height);
        g_velocity = legacy::speedFromUnits(8100);
        g_knots = 300;
        g_playerPlaneFlags = (gearUp ? 1 : 0) | (airBrake ? 8 : 0);
        g_gearDownArmed = 0;
        g_cornerSpeed = 100;
        g_joyRawX = g_joyRawY = 128;
        g_ViewX = legacy::viewX(0);
        g_ViewY = legacy::viewY(0);
        g_ourHead = {};
        g_ourPitch = legacy::angleFromWord(pitch);
        g_ourRoll = legacy::angleFromWord(roll);
        g_stallSpeed = {};
        g_liftForce = g_rollPitchTrim = {};
        g_orientationDirty = g_rotationCounter = g_rollWasNonzero = 0;
        rebuildOrientation();

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
        const int gees = std::min(128, int(g_rollGeeTable[(std::abs(roll) / 256) & 127]));
        const int pitchDrag = word(floorDivide(
            std::int64_t(rotation_reference::sine(pitch, g_angleLut)) * 80 + 16384, 32768));
        int targetSpeed = word((expected - pitchDrag) * 800 / 100);
        targetSpeed = word(floorDivide((height / 128 + 1024) * targetSpeed, 1024));
        targetSpeed = word(targetSpeed * (100 - remaining / 512) / 90);
        targetSpeed = word(floorDivide(targetSpeed * (128 - gees), 128));
        if (!gearUp) targetSpeed = word(targetSpeed - floorDivide(targetSpeed, 8));
        targetSpeed = std::clamp(targetSpeed, 0, 899) * 27;
        const int beforeBrakes = 8100 + ((targetSpeed - 8100) / 16) / hz;
        const int velocity = beforeBrakes - (airBrake ? (beforeBrakes / 16) / hz : 0);
        int root = 0;
        while ((root + 1) * (root + 1) <= gees * 4) ++root;
        const int corner = std::abs(word(root * word(100 * (height / 64 + 1024) / 1024) / 8));
        int lift = word(word(corner * 27) * 3072 / (std::abs(beforeBrakes) + 1));
        if (std::uint16_t(lift) > 8192) lift = 8192;
        const int trim = word(floorDivide(std::int64_t(word(lift - 768)) *
            rotation_reference::sine(roll + 16384, g_angleLut) + 16384, 32768));

        stepFlightModel();
        require(legacy::thrustUnits(g_thrust) == expected, "full flight model thrust response changed");
        require(g_fuelRemaining == remaining, "full flight model fuel cadence/depletion changed");
        require(g_setThrust == target, "damage thrust limit changed");
        require(g_gees == gees, "neutral banked-flight load changed");
        require(g_cornerSpeed == corner && AirspeedBoundary<FixedBackend>::stall(g_stallSpeed) == corner * 27,
                "full flight model corner/stall threshold changed");
        require(legacy::speedUnits(g_velocity) == velocity && g_knots == velocity / 27,
                "full flight model target-speed/acceleration order changed");
        require(legacy::signedAngle(g_liftForce) == lift && legacy::signedAngle(g_rollPitchTrim) == trim,
                "lift must sample accelerated speed before braking and the initial roll");
    }
    gameData = nullptr;
    commData = nullptr;
}
}
int main() { thrustAndFuel(); }
