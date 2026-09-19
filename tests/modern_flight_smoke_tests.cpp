#include "math/legacy_rotation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_propulsion.hpp"
#include "math/legacy_flight_control.hpp"
#include "egdata.h"
#include "egflight.h"
#include "comm.h"
#include "input.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

void stepFlightModel();
void rebuildOrientation();
void setupInstrumentLayoutFar();
void drawInstrumentGaugesFar();

using namespace f15::math;
static_assert(std::is_same_v<GameBackend, ModernBackend>);
static_assert(std::is_same_v<decltype(g_altitude), FlightAltitude<ModernBackend>>);
static_assert(std::is_same_v<decltype(g_velocity), FlightSpeed<ModernBackend>>);
static_assert(std::is_same_v<decltype(g_orientMatrix), Matrix3<ModernBackend>>);

void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using Angles = Boundary<ModernBackend>;
    using Altitudes = AltitudeBoundary<ModernBackend>;
    using Speeds = AirspeedBoundary<ModernBackend>;
    using Controls = ControlBoundary<ModernBackend>;
    test_headless_init();
    gfx_videoInit();
    gfx_setMode13();
    setupInstrumentLayoutFar();
    for (int altitude : {999, 1000, 65000, 65535, 65536, 90000}) {
        g_altitude = Altitudes::altitude(altitude);
        drawInstrumentGaugesFar();
        require(g_altRemainder == (altitude % 65536) % 1000,
                "legacy altitude tape baseline changed");
    }
    auto rollCommand = Controls::radiansPerSecond<RollAxis>(0.123456789);
    auto pitchCommand = Controls::radiansPerSecond<PitchAxis>(-0.234567891);
    require(legacy::updateControlFromWords(rollCommand, pitchCommand,
        [](int *, std::int16_t *) { return 0; }) == 0,
        "inactive control adapter changed callback result");
    require(Controls::radiansPerSecond(rollCommand) == 0.123456789 &&
            Controls::radiansPerSecond(pitchCommand) == -0.234567891,
            "inactive control adapter quantized modern rates");
    auto rollAngle = Angles::radians(0.123456789);
    auto pitchAngle = Angles::radians(-0.234567891);
    require(legacy::updateAttitudeFromWords(rollAngle, pitchAngle,
        [](std::int16_t *roll, std::int16_t *) { *roll = 16384; return 1; }) == 1,
        "active attitude adapter changed callback result");
    require(std::abs(Angles::radians(rollAngle) - 1.5707963267948966) < 1e-15 &&
            Angles::radians(pitchAngle) == -0.234567891,
            "single-axis attitude override quantized untouched axis");
    g_frameRateScaling = 120;
    g_ourHead = g_ourPitch = g_ourRoll = {};
    rebuildOrientation();
    const RotationDeltas<ModernBackend> delta{Angles::radians(1e-7), {}, {}};
    for (int i = 0; i < 1000; ++i) advanceFlightOrientation(delta);
    require(std::abs(Angles::radians(g_ourHead) - 1e-4) < 1e-12,
            "production orientation discarded modern fractional state");
    g_altitude = Altitudes::altitude(90000.25);
    g_velocity = Speeds::speed(8100.25);
    g_groundAltitude = g_autoLandingActive = 0;
    g_ourPitch = g_rollPitchTrim = {};
    advanceFlightAltitude();
    require(Altitudes::altitude(g_altitude) == 90000.25,
            "production modern altitude still has the fixed ceiling");
    g_velocity = Speeds::speed(50000.25);
    g_playerPlaneFlags = 1;
    brakeFlightSpeed();
    require(Speeds::speed(g_velocity) == 50000.25,
            "production modern speed still has the fixed cutoff");

    require(SDL_Init(SDL_INIT_GAMEPAD), "initialize input");
    Game game{};
    GameComm comm{};
    gameData = &game;
    commData = &comm;
    game.unk4 = 2;
    g_initPhase = 1;
    g_frameRateScaling = 15;
    frameTick = 1;
    g_thrust = legacy::thrustFromUnits(100);
    g_setThrust = 100;
    g_fuelRemaining = 5000;
    g_viewZ = 3000;
    g_altitude = Altitudes::altitude(3000.25);
    g_velocity = Speeds::speed(8100.25);
    g_knots = 300;
    g_kbdSensitivity = 2;
    g_ourHead = g_ourPitch = g_ourRoll = {};
    rebuildOrientation();
    input_setMode(INPUT_MODE_FLIGHT);
    for (int i = 0; i < 120; ++i) {
        stepFlightModel();
        require(std::isfinite(Altitudes::altitude(g_altitude)) &&
                Altitudes::altitude(g_altitude) > 0 && Speeds::speed(g_velocity) > 0,
                "modern flight step produced invalid airborne state");
        for (double coefficient : Angles::matrix(g_orientMatrix))
            require(std::isfinite(coefficient), "modern flight matrix became non-finite");
    }
    gameData = nullptr;
    commData = nullptr;
    SDL_Quit();
    std::puts("modern production flight smoke passed");
}
