#include "math/legacy_rotation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_propulsion.hpp"
#include "math/legacy_flight_control.hpp"
#include "math/aerodynamics.hpp"
#include "math/guidance.hpp"
#include "egdata.h"
#include "egflight.h"
#include "egkeys.h"
#include "comm.h"
#include "input.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

void stepFlightModel();
void updateFrame();
void rebuildOrientation();
void setupInstrumentLayoutFar();
void drawInstrumentGaugesFar();

using namespace f15::math;
static_assert(std::is_same_v<GameBackend, ModernBackend>);
static_assert(std::is_same_v<decltype(g_altitude), FlightAltitude<ModernBackend>>);
static_assert(std::is_same_v<decltype(g_velocity), FlightSpeed<ModernBackend>>);
static_assert(std::is_same_v<decltype(g_orientMatrix), Matrix3<ModernBackend>>);
static_assert(std::is_same_v<decltype(g_autopilotAltitude), RenderHeight<ModernBackend>>);
static_assert(!std::is_assignable_v<decltype(g_autopilotAltitude)&, int>);

void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    using Angles = Boundary<ModernBackend>;
    using Altitudes = AltitudeBoundary<ModernBackend>;
    using Speeds = AirspeedBoundary<ModernBackend>;
    using Controls = ControlBoundary<ModernBackend>;
    using Aero = AerodynamicsMath<ModernBackend>;
    using Guidance = GuidanceMath<ModernBackend>;
    const auto noseDown = Controls::radiansPerSecond<PitchAxis>(-0.1);
    constexpr double commandScale = 128 * (6.28318530717958647692 / 65536);
    for (const auto &sample : {std::pair{0.0, 32.0}, {299.5, 0.125}, {300.0, 0.0}}) {
        const auto corrected = Guidance::groundAvoidancePitch(
            Altitudes::altitude(sample.first), {}, {}, {});
        require(std::abs(Controls::radiansPerSecond(corrected) - sample.second * commandScale) < 1e-14,
                "modern ground-avoidance response changed");
    }
    require(Controls::radiansPerSecond(Guidance::groundAvoidancePitch(
                Altitudes::altitude(131072), {}, {}, noseDown)) == -0.1,
            "high-altitude ground avoidance replaced pilot pitch");
    const auto noseBelowHorizon = Angles::radians(-500 * (6.28318530717958647692 / 65536));
    require(std::abs(Controls::radiansPerSecond(Guidance::groundAvoidancePitch(
                Altitudes::altitude(400), noseBelowHorizon, {}, noseDown)) -
                6.25 * commandScale) < 1e-14,
            "descending flight-path ground avoidance changed");
    require(Controls::radiansPerSecond(Guidance::groundAvoidancePitch(
                Altitudes::altitude(400), noseBelowHorizon, noseBelowHorizon, noseDown)) == -0.1,
            "trim-neutral flight path incorrectly triggered ground avoidance");
    for (const auto &sample : {std::pair{0.0, 9}, {500.5, 4}, {999.0, 0},
                               {1000.0, 0}, {98304.0, 0}, {131072.0, 0}}) {
        require(Aero::lowAltitudeTurbulenceRange(Altitudes::altitude(sample.first),
                    Speeds::speed(8100)) == sample.second,
                "modern low-altitude turbulence response changed");
    }
    require(Aero::lowAltitudeTurbulenceRange(Altitudes::altitude(0),
                Speeds::speed(0)) == 0, "stationary aircraft received turbulence");
    test_headless_init();
    gfx_videoInit();
    gfx_setMode13();
    setupInstrumentLayoutFar();
    for (int altitude : {999, 1000, 65000, 65535, 65536, 90000, 100000, 1000000}) {
        g_altitude = Altitudes::altitude(altitude);
        drawInstrumentGaugesFar();
        require(g_altRemainder == altitude % 1000,
                "modern altitude tape wrapped at a legacy boundary");
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
    // Characterize the remaining scene-height narrowing before migrating its
    // gameplay consumers. It must not be mistaken for a flight-altitude limit.
    for (int altitude : {98303, 98304, 128000, 131072}) {
        g_altitude = Altitudes::altitude(altitude);
        advanceFlightAltitude();
        const int sceneHeight = (altitude - 16384) / 4 + 12288;
        const int signedWord = sceneHeight < 32768 ? sceneHeight : sceneHeight - 65536;
        require(Altitudes::altitude(g_altitude) == altitude,
                "high-altitude state changed during level flight");
        require(g_viewZ == signedWord,
                "scene-height narrowing baseline changed");
    }
    // Stall-warning caller regression: the height leg must use the full-range
    // scene height. At altitude 229376 the compressed scene height is 65536,
    // which wraps to word 0 in g_viewZ — below the 200-unit warning floor.
    g_groundAltitude = g_autoLandingActive = 0;
    for (double altitude : {0.0, 100.0, 199.5, 200.0, 800.0, 229376.0, 229376.5, 262144.0}) {
        g_altitude = Altitudes::altitude(altitude);
        g_velocity = Speeds::speed(0);
        g_ourPitch = g_rollPitchTrim = {};
        advanceFlightAltitude();
        const double scene = Altitudes::render(AltitudeMath<ModernBackend>::renderHeight(g_altitude));
        require(flightStallWarningRequired() == (scene >= 0 && scene < 200),
                "modern stall warning used the wrapped scene-height word");
    }
    // A sub-word nose-down must still trigger the warning.
    for (double pitch : {-1.0, -1e-5, -1e-9, 0.0, 1e-9, 1e-5, 1.0}) {
        g_ourPitch = Angles::radians(pitch);
        require(flightStallWarningRequired() == (pitch < 0),
                "modern stall warning quantized the pitch sign");
    }
    g_ourPitch = {};

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
    for (int difficulty : {1, 2})
    for (bool autopilot : {false, true})
    for (double altitude : {131072.0, 229376.0})
    for (int i = 0; i < 8; ++i) {
        game.unk4 = difficulty;
        g_altitude = Altitudes::altitude(altitude);
        g_velocity = Speeds::speed(8100);
        g_knots = 300;
        g_thrust = legacy::thrustFromUnits(100);
        g_setThrust = 100;
        g_fuelRemaining = 5000;
        g_playerPlaneFlags = 1;
        g_joyRawX = g_joyRawY = 128;
        g_autopilotAltitude = Altitudes::render(autopilot ? 10000 : 0);
        g_autopilotEngaged = 0;
        g_waypointBearing = 0;
        waypointIndex = 0;
        g_ourHead = g_ourPitch = g_ourRoll = g_rollPitchTrim = {};
        rebuildOrientation();
        advanceFlightAltitude();
        stepFlightModel();
        require(g_rollInput.isZero(),
                "high altitude injected false low-altitude roll turbulence");
        const double expectedPitch = autopilot ? -8 * commandScale : 0;
        require(std::abs(Controls::radiansPerSecond(g_pitchInput) - expectedPitch) < 1e-14,
                "high-altitude guidance used a wrapped scene height");
        const double expectedSpeed = 8100 + (899.0 * 27 - 8100) / (16 * 15);
        require(std::abs(Speeds::speed(g_velocity) - expectedSpeed) < 1e-10,
                "high-altitude propulsion used a wrapped scene height");
    }
    g_autopilotAltitude = {};
    g_altitude = Altitudes::altitude(131072.25);
    g_ourPitch = g_rollPitchTrim = {};
    advanceFlightAltitude();
    keyDispatch(SCAN_P);
    require(Altitudes::render(g_autopilotAltitude) == 40960.0625,
            "high-altitude autopilot capture lost range or precision");
    keyDispatch(SCAN_P);
    require(g_autopilotAltitude.isZero(), "autopilot toggle did not clear target");
    game.unk4 = 2;
    for (double altitude : {0.0, 0.125, 229376.0, 262144.0}) {
        g_altitude = Altitudes::altitude(altitude);
        g_velocity = Speeds::speed(1000);
        g_ourPitch = g_rollPitchTrim = {};
        advanceFlightAltitude();
        g_playerPlaneFlags = 9;
        brakeFlightSpeed();
        const double decrement = altitude == 0 ? 16.0 * 27 / 15 : 1000.0 / (16 * 15);
        require(std::abs(Speeds::speed(g_velocity) - (1000 - decrement)) < 1e-12,
                "scene-height wrap or quantization selected the wrong braking mode");
    }
    g_initPhase = 2;
    g_bulletTrackCount = 16;
    g_smokeSourceIdx = -1;
    for (double altitude : {0.0, 0.125, 131072.0, 229375.5, 229376.0, 229376.5, 262144.0}) {
        frameTick = 1;
        g_altitude = Altitudes::altitude(altitude);
        g_ourPitch = g_rollPitchTrim = {};
        advanceFlightAltitude();
        g_knots = 0;
        g_landingTimer = 0;
        g_nearestThreatRange = 0x7fff;
        g_groundAltitude = 0;
        updateFrame();
        require(g_landingTimer == (altitude == 0 ? 0 : 1),
                "mission ground contact lost altitude range or fractional precision");
    }
    gameData = nullptr;
    commData = nullptr;
    SDL_Quit();
    std::puts("modern production flight smoke passed");
}
