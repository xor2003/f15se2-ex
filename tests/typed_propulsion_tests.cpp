#include "math/legacy_propulsion.hpp"
#include "math/legacy_flight_control.hpp"
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
}
int main() { fixedParity(); modernResponse(); }
