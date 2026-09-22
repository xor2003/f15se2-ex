#include "math/legacy_altitude.hpp"
#include "math/interpolation.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_flight_control.hpp"
#include "math_rotation_reference.hpp"
#include "egdata.h"
#include "egflight.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
using FC = AltitudeBoundary<F>;
using MC = AltitudeBoundary<M>;
using FM = AltitudeMath<F>;
using MM = AltitudeMath<M>;
using rotation_reference::word;
void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
template<class Fn> void rejects(Fn fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception &) { threw = true; }
    require(threw, "invalid altitude operation accepted");
}
// Frozen from egflight.c/egframe.c at aa28521. Use explicitly unsigned
// storage and floor division so the test oracle is itself sanitizer-clean.
std::uint32_t constrain(std::uint32_t a, std::int16_t ground) {
    if (std::uint16_t(a) > 0xf230 || std::uint16_t(a) < std::uint16_t(ground))
        a = static_cast<std::uint32_t>(ground);
    if (a > 0xea60) a = 0xea60;
    return a;
}
std::int16_t render(std::uint32_t a) {
    if (a < 0x2000) return word(a);
    if (a < 0x4000) return word(((a - 0x2000) >> 1) + 0x2000);
    return word(((a - 0x4000) >> 2) + 0x3000);
}
std::int16_t climb(int velocity, int angle) {
    const auto p = std::int64_t(std::uint16_t(velocity) / 10) * rotation_reference::sine(angle, g_angleLut);
    return word(rotation_reference::floorDivide(p, 32768) +
                (rotation_reference::floorDivide(p, 16384) & 1));
}
std::uint32_t approach(std::uint32_t a, std::int16_t ground, int divisor) {
    a -= (a - std::uint32_t(ground)) / std::uint32_t(divisor);
    if (a < std::uint32_t(ground + 5)) a = std::uint32_t(ground + 5);
    return a;
}

void fixedMath() {
    const RotationMath<F> rotation(g_angleLut);
    for (int value = 0; value < 65536; ++value) {
        const auto a = FC::altitude(value);
        require(FC::render(FM::renderHeight(a)) == render(value), "render altitude compression changed");
        for (int ground : {-32768, -1, 0, 1, 100, 8191, 16384, 32767}) {
            require(FC::altitude(FM::constrain(a, FC::ground(ground))) == constrain(value, ground),
                    "fixed altitude limits changed");
            for (int divisor : {1, 2, 7, 14})
                require(FC::altitude(FM::landingApproach(a, FC::ground(ground), divisor)) ==
                        approach(value, ground, divisor), "landing approach changed");
        }
        for (int speed : {0, 1, 9, 10, 27, 32767, 32768, 65535}) {
            auto rate = FM::climb(FC::speed(speed), rotation.sine(Boundary<F>::angleWord(value)));
            require(FC::climb(rate) == climb(speed, value), "climb rounding changed");
        }
        auto rate = FM::climb(FC::speed(value), rotation.sine(Boundary<F>::angleWord(value * 17)));
        require(FC::climb(rate) == climb(value, value * 17), "airspeed word handling changed");
    }
    for (std::uint32_t value : {0u, 1u, 60000u, 62000u, 65535u, 65536u, 0x7fffffffu, 0xffffffffu}) {
        for (int v = -32768; v <= 32767; ++v) for (int hz : {1, 3, 4, 60, 32767}) {
            const auto actual = FM::integrate(FC::altitude(value), FC::climb(v), ControlBoundary<F>::frequency(hz));
            require(FC::altitude(actual) == value + std::uint32_t(v / hz), "altitude integration width changed");
        }
        require(FC::altitude(FM::obstacleEscape(FC::altitude(value))) == value + 500u,
                "obstacle escape wrapping changed");
    }
    rejects([] { FM::landingApproach({}, {}, 0); });
    // Scene-height band checks keep the original unsigned-word reading.
    for (int height = 0; height < 65536; ++height)
    for (int limit : {1, 200, 1000, 8192, 32767, -1})
        require(FM::belowSceneHeight(FC::render(static_cast<std::int16_t>(height)),
                                     FC::render(static_cast<std::int16_t>(limit))) ==
                (std::uint16_t(height) < std::uint16_t(limit)),
                "fixed scene-height band check changed");
    // Scene-height interpolation reproduces the legacy lerpLinear word math.
    for (int from : {-32768, -1, 0, 1, 199, 200, 8191, 16383, 32767})
    for (int to : {-32768, -1, 0, 1, 199, 200, 8191, 16383, 32767})
    for (auto tick : {std::pair{0, 1}, {1, 4}, {3, 4}, {1, 3}, {2, 3},
                      {4095, 4096}, {4096, 4096}, {1, 15}, {14, 15}}) {
        const int expected = static_cast<std::int16_t>(from +
            static_cast<std::int32_t>(std::int64_t(to - from) * tick.first / tick.second));
        require(FC::renderWord(FM::interpolate(FC::render(static_cast<std::int16_t>(from)),
                                             FC::render(static_cast<std::int16_t>(to)),
                                             FrameFraction::fromTicks(tick.first, tick.second))) == expected,
                "fixed scene-height interpolation differs from lerpLinear");
    }
    for (int height : {-32768, -1, 0, 1, 200, 32767})
        require(FC::renderWord(FC::render(static_cast<std::int16_t>(height))) == height,
                "fixed render word extraction changed");
    // Equality preserves the word comparison used by the deck-level check.
    for (int a : {-32768, -1, 0, 1, 0x80, 32767})
        for (int b : {-32768, -1, 0, 1, 0x80, 32767})
            require((FC::render(static_cast<std::int16_t>(a)) == FC::render(static_cast<std::int16_t>(b))) ==
                    (a == b), "fixed scene-height equality changed");
}

void productionCaller() {
    std::uint32_t seed = 12345;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed; };
    // Includes impossible/out-of-range states to freeze old width decisions;
    // the modern backend is tested by physical invariants instead.
    for (int tick = 0; tick < 24000; ++tick) {
        const auto initial = tick % 3 ? next() % 65536 : next();
        const auto pitch = word(next()), trim = word(next());
        const int speed = word(next());
        const auto ground = word(next());
        const int hz = 1 + next() % 120;
        g_altitude = FC::altitude(initial);
        g_ourPitch = Boundary<F>::angleWord(pitch);
        g_rollPitchTrim = Boundary<F>::angleWord(trim);
        g_velocity = legacy::speedFromUnits(speed);
        g_groundAltitude = ground;
        g_frameRateScaling = f15::math::SimRate::fromWord(hz);
        g_autoLandingActive = tick % 2;
        const auto rate = climb(speed, int(pitch) - trim);
        auto expected = initial;
        if (!g_autoLandingActive) expected += std::uint32_t(rate / hz);
        expected = constrain(expected, ground);
        advanceFlightAltitude();
        require(FC::climb(g_climbRate) == rate && FC::altitude(g_altitude) == expected &&
                g_viewZ == render(expected), "production vertical update differs from frozen baseline");
    }
}

void modernMath() {
    for (int ground : {-32768, -1, 0, 1, 128, 32767})
    for (int height : {-32768, -1, 0, 1, 128, 32767}) {
        require(FM::atGround(FC::render(height), FC::ground(ground)) == (height == ground),
                "fixed ground equality changed");
        require(FM::aboveGround(FC::render(height), FC::ground(ground)) ==
                    (std::uint16_t(height) > std::uint16_t(ground)),
                "fixed unsigned airborne comparison changed");
    }
    for (double ground : {0.0, 128.0, 32768.0})
    for (double offset : {-0.125, 0.0, 0.125, 65536.0}) {
        require(MM::atGround(MC::render(ground + offset), MC::ground(ground)) == (offset == 0),
                "modern ground equality lost fractional height");
        require(MM::aboveGround(MC::render(ground + offset), MC::ground(ground)) == (offset > 0),
                "modern airborne comparison wrapped or quantized");
    }
    for (int height = -32768; height <= 32767; ++height)
        require(FC::render(FM::captureAltitudeHold(FC::render(height))) ==
                    (height < 1000 ? 1000 : height),
                "fixed altitude-hold capture differs from original word policy");
    for (double height : {999.75, 1000.0, 1000.25, 32768.5, 40960.0625})
        require(MC::render(MM::captureAltitudeHold(MC::render(height))) ==
                    (height < 1000 ? 1000 : height),
                "modern altitude-hold capture lost range or precision");
    const RotationMath<M> rotation;
    const auto angle = Boundary<M>::radians(0.321);
    const auto rate = MM::climb(MC::speed(123.75), rotation.sine(angle));
    require(std::abs(MC::climb(rate) - 12.375 * std::sin(0.321)) < 1e-12, "modern climb quantized");
    auto altitude = MC::altitude(1000.125);
    const auto slowClimb = MC::climb(0.125);
    for (int i = 0; i < 12000; ++i)
        altitude = MM::integrate(altitude, slowClimb, ControlBoundary<M>::frequency(120));
    require(std::abs(MC::altitude(altitude) - 1012.625) < 1e-8, "modern altitude loses subunit increments");
    const auto frozen = FM::integrate(FC::altitude(1000), FC::climb(1), ControlBoundary<F>::frequency(120));
    require(FC::altitude(frozen) == 1000, "fixed integration acquired modern residual accumulation");
    for (double a : {8191.75, 8192.25, 16383.75, 16384.25, 60000.0}) {
        double expected = a < 8192 ? a : a < 16384 ? (a - 8192) / 2 + 8192 : (a - 16384) / 4 + 12288;
        require(MC::render(MM::renderHeight(MC::altitude(a))) == expected, "modern render height quantized");
    }
    require(MC::altitude(MM::constrain(MC::altitude(-0.25), MC::ground(10.5))) == 10.5,
            "modern descent does not stop at terrain");
    for (double height : {60000.25, 65536.5, 1000000.125})
        require(MC::altitude(MM::constrain(MC::altitude(height), {})) == height,
                "modern terrain constraint imposes a legacy flight ceiling");
    require(MC::altitude(MM::landingApproach(MC::altitude(10.125), {}, 4)) == 7.59375,
            "modern landing easing quantized");
    require(MC::altitude(MM::landingApproach({}, MC::ground(10.25), 4)) == 15.25,
            "modern landing floor ignored");
    for (double height : {8192.25, 12288.25, 20000.125, 60000.125}) {
        const auto onGround = MM::constrain({}, MC::ground(height));
        require(MC::render(MM::renderHeight(onGround)) == height,
                "modern terrain clamp mixes scene height with flight altitude");
    }
    // Modern band checks keep fractional precision and do not wrap at a word.
    for (double height : {-0.5, 0.0, 199.5, 200.0, 999.5, 1000.0, 32767.5, 32768.0, 65536.0})
        require(MM::belowSceneHeight(MC::render(height), MC::render(200.0)) == (height >= 0 && height < 200),
                "modern scene-height band wrapped or quantized");
    for (auto sample : {std::pair{1000.0, 500.0}, {65535.5, 65537.5}, {-4.0, 8.0}, {229376.0, 262144.0}})
        require(std::abs(MC::render(MM::interpolate(MC::render(sample.first), MC::render(sample.second),
                                                  FrameFraction::fromTicks(1, 4))) -
                         (sample.first * 0.75 + sample.second * 0.25)) < 1e-9,
                "modern scene-height interpolation lost precision");
    // The legacy render word wraps modulo 2^16 through a defined conversion.
    for (auto sample : {std::pair{0.0, 0}, {199.75, 199}, {32767.5, 32767}, {32768.0, -32768},
                        {65536.0, 0}, {65536.5, 0}, {-1.5, -1}, {73728.5, 8192}, {131072.0, 0}})
        require(MC::renderWord(MC::render(sample.first)) == sample.second,
                "modern render word narrowing changed");
    // Fractional heights stay distinct where the word comparison would alias.
    require(MC::render(128.0) == MC::render(128.0) &&
            MC::render(128.4) != MC::render(128.0), "modern scene-height equality quantized");
    rejects([] { MC::speed(-1); });
    rejects([] { MC::altitude(std::numeric_limits<double>::infinity()); });
    rejects([] { MC::climb(std::numeric_limits<double>::quiet_NaN()); });
    rejects([] { MM::integrate(MC::altitude(std::numeric_limits<double>::max()),
                               MC::climb(std::numeric_limits<double>::max()), ControlBoundary<M>::seconds(1)); });
}
}
int main() {
    fixedMath(); productionCaller(); modernMath();
    std::puts("typed altitude tests passed");
}
