#include "math/aerodynamics.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_altitude.hpp"
#include "math/interpolation.hpp"
#include "math_rotation_reference.hpp"
#include "egdata.h"
#include "egflight.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
using FM = AerodynamicsMath<F>;
using MM = AerodynamicsMath<M>;
using FC = AirspeedBoundary<F>;
using MC = AirspeedBoundary<M>;
using rotation_reference::word;
void require(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
template<class Fn> void rejects(Fn fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception &) { threw = true; }
    require(threw, "invalid aerodynamic operation accepted");
}
// Frozen from egflight.c at 8c0f982, with defined widened reference arithmetic.
int lift(int stall, int velocity) {
    const auto magnitude = velocity < 0 ? -std::int64_t(velocity) : std::int64_t(velocity);
    const int result = word(std::int64_t(word(stall)) * 3072 / (magnitude + 1));
    return std::uint16_t(result) > 8192 ? 8192 : result;
}
int trim(int correction, int roll) {
    const auto product = std::int64_t(word(correction - 768)) * rotation_reference::sine(roll + 16384, g_angleLut);
    return word(rotation_reference::floorDivide(product, 32768) +
                (rotation_reference::floorDivide(product, 16384) & 1));
}
void fixedMath() {
    const RotationMath<F> rotation(g_angleLut);
    for (int value = 0; value < 65536; ++value) {
        for (int speed : {-65536, -32768, -1, 0, 1, 2700, 8100, 32767, 65536, INT32_MAX - 1, INT32_MIN + 2})
            require(legacy::signedAngle(FM::liftCorrection(FC::stall(word(value)), FC::speed(speed))) == lift(value, speed),
                    "lift signed quotient/word clamp changed");
        for (int stall : {-32768, -1, 0, 1, 2700, 32767})
            require(legacy::signedAngle(FM::liftCorrection(FC::stall(stall), FC::speed(value))) == lift(stall, value),
                    "lift velocity denominator changed");
        for (int correction : {0, 1, 767, 768, 769, 8192, 32767, -32768})
            require(legacy::signedAngle(FM::pitchTrim(legacy::angleFromWord(correction), rotation.cosine(legacy::angleFromWord(value)))) ==
                    trim(correction, value), "pitch trim cosine/word rounding changed");
    }
    for (int bad : {INT32_MIN, INT32_MIN + 1, INT32_MAX})
        rejects([&] { FM::liftCorrection(FC::stall(2700), FC::speed(bad)); });
    // Signed linear interpolation intentionally differs from shortest-arc pose interpolation.
    for (int a : {-32768, -8192, -1, 0, 1, 8192, 32767})
        for (int b : {-32768, -8192, -1, 0, 1, 8192, 32767})
            for (int n = 0; n <= 17; ++n) {
                const auto result = PoseInterpolation<F>::linearOffset(legacy::angleFromWord(a), legacy::angleFromWord(b),
                                                                      FrameFraction::fromTicks(n, 17));
                require(legacy::signedAngle(result) == a + (b - a) * n / 17, "trim signed linear interpolation changed");
            }
    rejects([] { PoseInterpolation<F>::linearOffset(legacy::angleFromWord(-32768), legacy::angleFromWord(32767),
                 FrameFraction::fromTicks(INT64_MAX / 32768, INT64_MAX / 32768)); });
}
void productionCaller() {
    std::uint32_t random = 42;
    auto next = [&] { random = random * 1664525u + 1013904223u; return random; };
    for (int tick = 0; tick < 24000; ++tick) {
        const auto stall = word(next()), roll = word(next()), pitch = word(next());
        const int speed = int(next() % 262144) - 131072;
        g_velocity = FC::speed(speed);
        g_stallSpeed = FC::stall(stall);
        g_ourRoll = legacy::angleFromWord(roll);
        g_ourPitch = legacy::angleFromWord(pitch);
        updateFlightLift();
        const int expectedLift = lift(stall, speed), expectedTrim = trim(expectedLift, roll);
        require(legacy::signedAngle(g_liftForce) == expectedLift && legacy::signedAngle(g_rollPitchTrim) == expectedTrim,
                "production lift/trim differs from frozen baseline");
        g_autoLandingActive = 1;
        g_altitude = AltitudeBoundary<F>::altitude(1000);
        g_groundAltitude = 0;
        advanceFlightAltitude();
        const auto product = std::int64_t(std::uint16_t(speed) / 10) * rotation_reference::sine(pitch - expectedTrim, g_angleLut);
        const int expectedClimb = word(rotation_reference::floorDivide(product, 32768) +
                                      (rotation_reference::floorDivide(product, 16384) & 1));
        require(legacy::climbUnits(g_climbRate) == expectedClimb, "typed trim changed production flight-path climb");
    }
    const auto savedLift = g_liftForce, savedTrim = g_rollPitchTrim;
    g_velocity = FC::speed(INT32_MIN);
    rejects([] { updateFlightLift(); });
    require(g_liftForce == savedLift && g_rollPitchTrim == savedTrim, "invalid lift partially changed state");
}
void modernMath() {
    constexpr double radiansPerUnit = 6.28318530717958647692 / 65536;
    const RotationMath<M> rotation;
    for (int tick = 0; tick < 12000; ++tick) {
        const double speed = 8100.125 + tick * 0.001;
        const double roll = -3.0 + tick * 0.0005;
        const double expectedLift = 2700.375 / (speed + 1) * 3072 * radiansPerUnit;
        const double expectedTrim = (expectedLift - 768 * radiansPerUnit) * std::cos(roll);
        const auto correction = MM::liftCorrection(MC::stall(2700.375), MC::speed(speed));
        const auto offset = MM::pitchTrim(correction, rotation.cosine(Boundary<M>::radians(roll)));
        require(std::abs(Boundary<M>::radians(correction) - expectedLift) < 1e-14 &&
                std::abs(Boundary<M>::radians(offset) - expectedTrim) < 1e-14, "modern lift/trim quantizes");
        const auto rate = AltitudeMath<M>::climb(AirspeedMath<M>::verticalSample(MC::speed(speed)),
            rotation.sine(Boundary<M>::radians(0.125) - offset));
        require(std::abs(AltitudeBoundary<M>::climb(rate) - speed / 10 * std::sin(0.125 - expectedTrim)) < 1e-10,
                "modern trim-to-climb pipeline quantizes");
    }
    require(Boundary<M>::radians(MM::liftCorrection(MC::stall(32767.5), {})) == 8192 * radiansPerUnit,
            "modern lift clamp wraps");
    require(Boundary<M>::radians(MM::liftCorrection(MC::stall(-0.25), {})) == 0,
            "modern negative lift should clamp, not wrap");
    const auto interpolated = PoseInterpolation<M>::linearOffset(Boundary<M>::radians(0.00001),
        Boundary<M>::radians(0.00003), FrameFraction::fromTicks(1, 4));
    require(std::abs(Boundary<M>::radians(interpolated) - 0.000015) < 1e-18, "modern trim interpolation quantizes");
    require(std::isfinite(Boundary<M>::radians(MM::liftCorrection(MC::stall(1e308), MC::speed(1e308)))),
            "modern lift ratio overflowed before division");
    rejects([] { MM::liftCorrection(MC::stall(1e308), {}); });
}
}
int main() { fixedMath(); productionCaller(); modernMath(); }
