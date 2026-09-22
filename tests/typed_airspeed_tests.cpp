#include "math/legacy_airspeed.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_flight_control.hpp"
#include "math/legacy_horizontal.hpp"
#include "math/legacy_altitude.hpp"
#include "math_rotation_reference.hpp"
#include "egdata.h"
#include "egflight.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
using FC = AirspeedBoundary<F>;
using MC = AirspeedBoundary<M>;
using FM = AirspeedMath<F>;
using MM = AirspeedMath<M>;
void require(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
template<class Fn> void rejects(Fn fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception &) { threw = true; }
    require(threw, "invalid airspeed operation accepted");
}
// Frozen expressions from egflight.c at 08b8c7b, before typed airspeed.
// Test only defined native-int arithmetic; exercise rejected overflows separately.
int accelerate(int speed, int target, int hz) { return speed + ((target - speed) / 16) / hz; }
int brake(int speed, int hz, bool active, bool onGround, bool carrier, int difficulty) {
    if (active) {
        if (onGround) {
            speed -= (-(difficulty * 8 - 32) * 27) / hz;
            if (carrier && std::uint16_t(speed) < 432) speed = 0;
        } else speed -= (std::uint16_t(speed) >> 4) / hz;
    }
    if (std::uint16_t(speed) > 45000) speed = 0;
    return speed;
}
std::int16_t projected(int speed, int angle) {
    const auto p = std::int64_t(rotation_reference::word(speed)) *
                   rotation_reference::sine(angle + 16384, g_angleLut);
    return rotation_reference::word(rotation_reference::floorDivide(p, 32768) +
                                   (rotation_reference::floorDivide(p, 16384) & 1));
}
void fixedMath() {
    const RotationMath<F> rotation(g_angleLut);
    for (int value = 0; value < 65536; ++value) {
        // The signed/native views are deliberately different from the low word.
        for (int speed : {value, value - 65536, value + 65536}) {
            const auto s = FC::speed(speed);
            for (int hz : {1, 4, 7, 60, 120}) {
                const auto step = ControlBoundary<F>::frequency(hz);
                require(FC::speed(FM::accelerate(s, FC::speed(8100), step)) == accelerate(speed, 8100, hz),
                        "native airspeed acceleration changed");
                require(FC::speed(FM::airBrake(s, step)) == speed - (std::uint16_t(speed) >> 4) / hz,
                        "air brake word sampling changed");
                for (int difficulty = 0; difficulty <= 3; ++difficulty) {
                    auto slowed = FM::groundBrake(s, FC::deceleration((32 - difficulty * 8) * 27), step);
                    require(FC::speed(FM::constrain(FM::carrierStop(slowed))) ==
                            brake(speed, hz, true, true, true, difficulty), "ground/carrier braking changed");
                }
            }
            const int angle = rotation_reference::word(value * 17);
            require(HorizontalBoundary<F>::speed(FM::horizontalSample(s, rotation.cosine(Boundary<F>::angleWord(angle)))) ==
                    projected(speed, angle), "horizontal signed word sample changed");
            const auto rate = AltitudeMath<F>::climb(FM::verticalSample(s), rotation.sine(Boundary<F>::angleWord(angle)));
            const auto old = AltitudeMath<F>::climb(AltitudeBoundary<F>::speed(std::uint16_t(speed)),
                                                   rotation.sine(Boundary<F>::angleWord(angle)));
            require(AltitudeBoundary<F>::climb(rate) == AltitudeBoundary<F>::climb(old), "vertical unsigned sample changed");
        }
        for (int speed : {-32768, -1, 0, 1, 32767, 65535})
            require(HorizontalBoundary<F>::speed(FM::horizontalSample(FC::speed(speed), rotation.cosine(Boundary<F>::angleWord(value)))) ==
                    projected(speed, value), "horizontal projection rounding changed");
    }
    const auto step = ControlBoundary<F>::frequency(1);
    require(FC::speed(FM::accelerate(FC::speed(INT32_MAX), FC::speed(INT32_MAX - 1000), step)) == INT32_MAX - 62,
            "native positive speed narrowed");
    require(FC::speed(FM::accelerate(FC::speed(INT32_MIN), FC::speed(INT32_MIN + 1000), step)) == INT32_MIN + 62,
            "native negative speed narrowed");
    rejects([&] { FM::accelerate(FC::speed(INT32_MIN), FC::speed(INT32_MAX), step); });
    rejects([&] { FM::groundBrake(FC::speed(INT32_MIN), FC::deceleration(1), step); });
    rejects([&] { FM::groundBrake(FC::speed(INT32_MAX), FC::deceleration(-1), step); });
    // Indicated knots keep the legacy `speedWord(v) / 27` truncation: only the
    // low 16 bits count, and the quotient truncates toward zero.
    for (int value = 0; value < 65536; ++value)
        for (int speed : {value, value - 65536, value + 65536})
            require(FC::corner(FM::indicatedKnots(FC::speed(speed))) ==
                    static_cast<std::int16_t>(std::uint16_t(value) / 27),
                    "fixed indicated-knots conversion changed");
    // speedFromKnots reproduces `speedFromUnits(g_knots * 27)` exactly.
    for (int knots : {-32768, -1, 0, 1, 199, 350, 2427, 32767})
        require(FC::speed(FM::speedFromKnots(FC::corner(static_cast<std::int16_t>(knots)))) ==
                static_cast<std::int32_t>(static_cast<std::int16_t>(knots)) * 27,
                "fixed speed-from-knots reconstruction changed");
    // Threshold construction and same-unit comparisons match the old int16 domain.
    for (int threshold : {-32768, -1, 0, 1, 250, 350, 2427, 32767}) {
        require(FC::corner(FM::knots(threshold)) == static_cast<std::int16_t>(threshold),
                "fixed knots threshold changed");
        require((FC::corner(350) > FM::knots(threshold)) == (350 > static_cast<std::int16_t>(threshold)),
                "fixed knots comparison changed");
    }
    // Projectile launch speed keeps the `speedWord(v) >> 11` low-word read.
    for (int value = 0; value < 65536; ++value)
        for (int speed : {value, value - 65536, value + 65536})
            require(FC::projectile(FC::speed(speed)) ==
                    static_cast<std::int16_t>(std::uint16_t(value) >> 11),
                    "fixed projectile launch speed changed");
}
void productionCaller() {
    Game data{};
    auto *saved = gameData;
    gameData = &data;
    std::uint32_t random = 13;
    auto next = [&] { random = random * 1664525u + 1013904223u; return random; };
    int expected = 0;
    for (int tick = 0; tick < 24000; ++tick) {
        if (tick % 97 == 0) {
            expected = int(next() % 262144) - 131072;
            g_velocity = FC::speed(expected);
        }
        const int target = next() % 24274;
        g_frameRateScaling = f15::math::SimRate::fromWord(1 + next() % 120);
        data.unk4 = next() % 4;
        const bool active = next() % 3 != 0, onGround = next() % 3 != 0, carrier = next() % 3 != 0;
        g_playerPlaneFlags = active ? 8 : 0;
        g_groundAltitude = carrier ? 128 : 0;
        g_viewZ = g_groundAltitude + (onGround ? 0 : 1);
        expected = accelerate(expected, target, g_frameRateScaling.word());
        accelerateFlightSpeed(FC::speed(target));
        require(FC::speed(g_velocity) == expected, "production acceleration differs from frozen baseline");
        expected = brake(expected, g_frameRateScaling.word(), active, onGround, carrier, data.unk4);
        brakeFlightSpeed();
        require(FC::speed(g_velocity) == expected, "production braking differs from frozen baseline");
    }
    gameData = saved;
}
void modernMovement() {
    const RotationMath<M> rotation;
    const auto pitch = Boundary<M>::radians(0.125), heading = Boundary<M>::radians(0.375);
    const auto step = ControlBoundary<M>::frequency(120);
    auto speed = MC::speed(0.125);
    ViewCoordinate<M, ViewXAxis> x;
    ViewCoordinate<M, ViewYAxis> y;
    auto altitude = AltitudeBoundary<M>::altitude(1000.125);
    double expectedSpeed = 0.125, expectedX = 0, expectedY = 0, expectedAltitude = 1000.125;
    for (int i = 0; i < 12000; ++i) {
        speed = MM::accelerate(speed, MC::speed(1.25), step);
        const auto movement = HorizontalMath<M>::increments(MM::horizontalSample(speed, rotation.cosine(pitch)),
            rotation.sine(heading), rotation.cosine(heading), step);
        x += movement.x;
        y += movement.y;
        const auto climb = AltitudeMath<M>::climb(MM::verticalSample(speed), rotation.sine(pitch));
        altitude = AltitudeMath<M>::integrate(altitude, climb, step);
        expectedSpeed += (1.25 - expectedSpeed) / 16 / 120;
        expectedX += expectedSpeed * std::cos(0.125) * std::sin(0.375) / 10 / 120;
        expectedY += expectedSpeed * std::cos(0.125) * std::cos(0.375) / 10 / 120;
        expectedAltitude += expectedSpeed / 10 * std::sin(0.125) / 120;
    }
    require(std::abs(HorizontalBoundary<M>::coordinate(x) - expectedX) < 1e-10 &&
            std::abs(HorizontalBoundary<M>::coordinate(y) - expectedY) < 1e-10 &&
            std::abs(AltitudeBoundary<M>::altitude(altitude) - expectedAltitude) < 1e-9,
            "modern typed speed-to-position pipeline loses precision");
}
void modernMath() {
    auto speed = MC::speed(1000.125);
    double expected = 1000.125;
    const auto step = ControlBoundary<M>::frequency(120);
    for (int i = 0; i < 12000; ++i) {
        expected += (1001.25 - expected) / 16 / 120;
        speed = MM::accelerate(speed, MC::speed(1001.25), step);
    }
    require(std::abs(MC::speed(speed) - expected) < 1e-10 && MC::speed(speed) > 1001.24,
            "modern acceleration loses subunit increments");
    speed = MC::speed(1000.125);
    for (int i = 0; i < 12000; ++i) speed = MM::groundBrake(speed, MC::deceleration(0.125), step);
    require(std::abs(MC::speed(speed) - 987.625) < 1e-8, "modern braking quantizes");
    require(MC::speed(MM::airBrake(MC::speed(65536.5), ControlBoundary<M>::frequency(1))) == 65536.5 * 15 / 16,
            "modern airbrake uses low word");
    require(MC::speed(MM::constrain(MC::speed(-0.25))) == 0, "modern speed magnitude is negative");
    for (double value : {45000.25, 65536.5, 1000000.125})
        require(MC::speed(MM::constrain(MC::speed(value))) == value,
                "modern speed constraint imposes a legacy ceiling");
    require(MC::speed(MM::carrierStop(MC::speed(431.999))) == 0 &&
            MC::speed(MM::carrierStop(MC::speed(432.125))) == 432.125, "modern carrier threshold changed");
    const RotationMath<M> rotation;
    const auto angle = Boundary<M>::radians(0.321);
    require(HorizontalBoundary<M>::speed(MM::horizontalSample(MC::speed(123.75), rotation.cosine(angle))) ==
            123.75 * std::cos(0.321), "modern horizontal sample quantizes");
    const auto climb = AltitudeMath<M>::climb(MM::verticalSample(MC::speed(123.75)), rotation.sine(angle));
    require(std::abs(AltitudeBoundary<M>::climb(climb) - 12.375 * std::sin(0.321)) < 1e-12,
            "modern vertical sample quantizes");
    // Indicated knots keep fractional speed and do not wrap at the 16-bit word.
    for (double speed : {0.0, 26.9, 27.0, 5400.5, 65535.0, 65536.0, 70000.25, 1e6})
        require(std::abs(MC::corner(MM::indicatedKnots(MC::speed(speed))) - speed / 27) < 1e-9,
                "modern indicated knots wrapped or quantized");
    for (double knots : {0.5, 199.75, 350.0, 2427.5})
        require(std::abs(MC::speed(MM::speedFromKnots(MC::corner(knots))) - knots * 27) < 1e-9,
                "modern speed-from-knots reconstruction quantizes");
    require(MC::corner(MM::knots(350)) == 350.0, "modern knots threshold quantized");
    require(MM::knots(200) < MM::indicatedKnots(MC::speed(5400.5)) &&
            MM::indicatedKnots(MC::speed(5400.5)) < MM::knots(201),
            "modern knots comparisons quantized");
    // Projectile launch speed keeps the unwrapped quotient (65536 wraps to 0
    // under fixed) and saturates at the int16 field rather than overflowing.
    for (double speed : {0.0, 2047.9, 2048.5, 65536.0, 70000.25})
        require(MC::projectile(MC::speed(speed)) == static_cast<std::int16_t>(speed / 2048),
                "modern projectile launch speed wrapped or quantized");
    require(MC::projectile(MC::speed(1e9)) == 32767, "modern projectile launch speed did not saturate");
    rejects([] { MC::speed(std::numeric_limits<double>::infinity()); });
    rejects([] { MC::deceleration(std::numeric_limits<double>::quiet_NaN()); });
    rejects([] { MM::verticalSample(MC::speed(-1)); });
    rejects([&] { MM::accelerate(MC::speed(-1e308), MC::speed(1e308), step); });
}
}
int main() { fixedMath(); productionCaller(); modernMath(); modernMovement(); }
