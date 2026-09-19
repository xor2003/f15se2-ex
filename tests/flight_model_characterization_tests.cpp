#include "math/legacy_airspeed.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
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
    for (bool fuelTick : {false, true}) {
        g_initPhase = 1;
        g_frameRateScaling = hz;
        frameTick = fuelTick ? hz * 2 : 1;
        g_thrust = initial;
        g_setThrust = requested;
        g_fuelRemaining = fuel;
        g_gunHits = damage;
        g_hudVisible = g_inputDisabled = 0;
        g_autopilotAltitude = g_autopilotEngaged = 0;
        g_ejectState = g_autoCrashDive = g_currentWeaponType = 0;
        g_groundAltitude = 0;
        g_viewZ = 2000;
        g_altitude = legacy::altitudeFromUnits(2000);
        g_velocity = legacy::speedFromUnits(8100);
        g_knots = 300;
        g_playerPlaneFlags = 1;
        g_joyRawX = g_joyRawY = 128;
        g_ourHead = g_ourPitch = g_ourRoll = {};
        g_orientationDirty = g_rotationCounter = g_rollWasNonzero = 0;
        rebuildOrientation();

        const int target = damage ? std::min(requested, std::max(0, 144 - damage * 4)) : requested;
        int expected = initial + ((target - initial) / 4) / hz;
        if (target > expected) ++expected;
        if (target < expected) expected = target;
        int remaining = fuel;
        if (fuelTick && target) remaining -= target * target / 750 + 2;
        if (remaining <= 0) { remaining = 0; expected = 0; }

        // Level, neutral-stick flight has 16 load units. Preserve each integer
        // division separately: algebraic simplification changes truncation.
        int targetSpeed = expected * 800 / 100;
        targetSpeed = ((2000 / 128 + 1024) * targetSpeed) / 1024;
        targetSpeed = targetSpeed * (100 - remaining / 512) / 90;
        targetSpeed = targetSpeed * (128 - 16) / 128;
        targetSpeed = std::clamp(targetSpeed, 0, 899) * 27;
        const int velocity = 8100 + ((targetSpeed - 8100) / 16) / hz;
        const int corner = 100 * (2000 / 64 + 1024) / 1024;

        stepFlightModel();
        require(g_thrust == expected, "full flight model thrust response changed");
        require(g_fuelRemaining == remaining, "full flight model fuel cadence/depletion changed");
        require(g_setThrust == target, "damage thrust limit changed");
        require(g_gees == 16, "neutral level-flight load changed");
        require(g_cornerSpeed == corner && AirspeedBoundary<FixedBackend>::stall(g_stallSpeed) == corner * 27,
                "full flight model corner/stall threshold changed");
        require(legacy::speedUnits(g_velocity) == velocity && g_knots == velocity / 27,
                "full flight model target-speed/acceleration order changed");
    }
    gameData = nullptr;
    commData = nullptr;
}
}
int main() { thrustAndFuel(); }
