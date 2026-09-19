#include "math/legacy_propulsion.hpp"
#include "math/legacy_flight_control.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_rotation.hpp"
#include "math_rotation_reference.hpp"
#include "egdata.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
using FB = PropulsionBoundary<F>;
using MB = PropulsionBoundary<M>;
void require(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
template<class Fn> void rejects(Fn fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception &) { threw = true; }
    require(threw, "invalid propulsion operation accepted");
}
void fixedParity() {
    for (int current = -32768; current <= 32767; ++current) {
        for (int target : {-32768, -1, 0, 1, 35, 100, 144, 32767}) {
            for (int hz : {1, 4, 15, 120}) {
                // Frozen from egflight.c at a701aac, before this extraction.
                int expected = current + ((target - current) / 4) / hz;
                if (target > expected) ++expected;
                if (target < expected) expected = target;
                const auto result = PropulsionMath<F>::advance(FB::thrust(current), FB::thrust(target),
                                                               ControlBoundary<F>::frequency(hz));
                require(FB::thrust(result) == expected, "fixed engine ramp differs from baseline");
            }
        }
        for (int hits : {-32768, -1, 0, 1, 12, 35, 36, 40, 32767}) {
            int expected = current;
            require(PropulsionMath<F>::requiresDamageLimit(FB::thrust(current), hits) ==
                    (hits != 0 && current > 144 - hits * 4), "damage gauge refresh condition changed");
            if (hits != 0 && expected > 144 - hits * 4) {
                expected = 144 - hits * 4;
                if (expected < 0) expected = 0;
            }
            require(FB::thrust(PropulsionMath<F>::limitForDamage(FB::thrust(current), hits)) == expected,
                    "fixed engine damage limit differs from baseline");
        }
    }
}
void modernResponse() {
    const auto target = MB::thrust(144.0);
    for (double seconds : {0.125, 1.0, 4.0, 20.0}) {
        const double expected = std::min(144.0, 204 * (1 - std::exp(-seconds / 4)));
        for (int subdivisions : {1, 2, 15, 60, 240}) {
            const int ticks = int(std::ceil(seconds)) * subdivisions;
            EngineThrust<M> current;
            const auto step = ControlBoundary<M>::seconds(seconds / ticks);
            for (int tick = 0; tick < ticks; ++tick) {
                const auto next = PropulsionMath<M>::advance(current, target, step);
                require(MB::thrust(next) >= MB::thrust(current) && MB::thrust(next) <= 144,
                        "modern ramp overshoots or decreases");
                current = next;
            }
            require(std::abs(MB::thrust(current) - expected) < 1e-10, "modern ramp depends on tick count");
        }
    }
    const auto step = ControlBoundary<M>::seconds(0.01);
    const auto tiny = PropulsionMath<M>::advance({}, MB::thrust(1.25), step);
    require(MB::thrust(tiny) > 0 && MB::thrust(tiny) < 1, "modern thrust lost fractional response");
    require(PropulsionMath<M>::advance(target, MB::thrust(35.125), step) == MB::thrust(35.125),
            "throttle reduction must remain immediate");
    require(PropulsionMath<M>::advance(target, target, step) == target, "steady thrust changed");
    require(PropulsionMath<M>::limitForDamage(MB::thrust(95.125), 12) == MB::thrust(95.125),
            "modern damage limit rounded a sub-ceiling command");
    require(PropulsionMath<M>::limitForDamage(target, 12) == MB::thrust(96), "modern damage cap changed");
    require(PropulsionMath<M>::limitForDamage(target, 40).isZero(), "severe damage must cap thrust at zero");
    rejects([] { MB::thrust(std::numeric_limits<double>::infinity()); });
    rejects([] { MB::thrust(std::numeric_limits<double>::quiet_NaN()); });
    rejects([&] { PropulsionMath<M>::advance(MB::thrust(-1e308), MB::thrust(1e308), step); });
}
void fixedTargetSpeed() {
    using rotation_reference::word;
    using rotation_reference::floorDivide;
    const RotationMath<F> math(g_angleLut);
    for (int thrust : {-32768, -1, 0, 1, 35, 100, 144, 32767})
    for (int pitch : {-32768, -16384, -4096, 0, 4096, 16384, 32767})
    for (int height : {-32768, -1, 0, 127, 128, 4095, 8192, 32767})
    for (int fuel : {-32768, -513, -1, 0, 1, 511, 512, 5000, 32767})
    for (int load : {-128, -1, 0, 16, 64, 127, 128, 129})
    for (auto gear : {LandingGear::Retracted, LandingGear::Extended}) {
        // Frozen stage-by-stage formula from de3ab99, with independent LUT
        // interpolation and signed-word stores rather than backend helpers.
        const int drag = word(floorDivide(std::int64_t(rotation_reference::sine(pitch, g_angleLut)) * 80 + 16384, 32768));
        int expected = word((thrust - drag) * 800 / 100);
        expected = word(floorDivide((std::uint16_t(height) / 128 + 1024) * expected, 1024));
        expected = word(expected * (100 - floorDivide(fuel, 512)) / 90);
        expected = word(floorDivide(expected * (128 - load), 128));
        if (gear == LandingGear::Extended) expected = word(expected - floorDivide(expected, 8));
        expected = std::clamp(expected, 0, 899) * 27;
        const auto result = PropulsionMath<F>::targetSpeed(FB::thrust(thrust),
            math.sine(legacy::angleFromWord(pitch)), AltitudeBoundary<F>::render(std::int16_t(height)),
            FB::fuel(fuel), FB::load(load), gear);
        require(AirspeedBoundary<F>::speed(result) == expected, "fixed target-speed stages differ from baseline");
    }
    rejects([&] { PropulsionMath<F>::targetSpeed(FB::thrust(100), {}, {}, {},
        FB::load(INT32_MIN), LandingGear::Retracted); });
    rejects([&] { PropulsionMath<F>::targetSpeed(FB::thrust(100), {}, {}, {},
        FB::load(INT32_MAX), LandingGear::Retracted); });
}
void modernTargetSpeed() {
    const RotationMath<M> math;
    for (double thrust : {0.125, 35.125, 100.0, 144.0})
    for (double pitch : {-0.5, 0.0, 0.5})
    for (double height : {0.0, 127.25, 8192.125})
    for (double fuel : {0.125, 511.25, 5000.125})
    for (double load : {0.0, 16.125, 64.0, 128.0}) {
        const auto sine = math.sine(Boundary<M>::radians(pitch));
        const double freeSpeed = (thrust - std::sin(pitch) * 80) * 8 *
            (1 + height / 131072) * (100 - fuel / 512) / 90 * (1 - load / 128);
        for (auto gear : {LandingGear::Retracted, LandingGear::Extended}) {
            const double expected = std::clamp(freeSpeed * (gear == LandingGear::Extended ? 0.875 : 1.0), 0.0, 899.0) * 27;
            const auto result = PropulsionMath<M>::targetSpeed(MB::thrust(thrust), sine,
                AltitudeBoundary<M>::render(height), MB::fuel(fuel), MB::load(load), gear);
            require(std::abs(AirspeedBoundary<M>::speed(result) - expected) < 1e-9,
                    "modern target speed lost continuous scaling");
        }
    }
    rejects([] { MB::fuel(std::numeric_limits<double>::quiet_NaN()); });
    rejects([] { MB::load(std::numeric_limits<double>::infinity()); });
    rejects([] { PropulsionMath<M>::targetSpeed(MB::thrust(1e308), {}, {}, {}, {}, LandingGear::Retracted); });
}
}
int main() { fixedParity(); modernResponse(); fixedTargetSpeed(); modernTargetSpeed(); }
