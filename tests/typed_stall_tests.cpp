#include "math/aerodynamics.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_flight_control.hpp"
#include "egdata.h"
#include "egflight.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
using FM = AerodynamicsMath<F>;
using MM = AerodynamicsMath<M>;
using FC = AirspeedBoundary<F>;
using MC = AirspeedBoundary<M>;
using FA = AltitudeBoundary<F>;
using MA = AltitudeBoundary<M>;
using MB = Boundary<M>;
void require(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
int word(std::int64_t value) {
    const auto bits = std::uint16_t(value);
    return bits < 32768 ? int(bits) : int(bits) - 65536;
}
template<class Fn> void rejects(Fn fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception &) { threw = true; }
    require(threw, "invalid stall operation accepted");
}
void fixedMath() {
    // Freeze unsigned word comparisons and the two per-tick shifts from 02e55d1.
    for (int value = 0; value < 65536; ++value) {
        const auto threshold = legacy::stallFromUnits(value);
        require(FC::stall(legacy::stallFromUnits(word(value) * 27)) == word(word(value) * 27),
                "corner-to-stall word conversion changed");
        for (int velocity : {-65536, -32768, -1, 0, 1, 432, 2700, 32767, 32768, 65535, 65536, 65537}) {
            const auto speed = FC::speed(velocity);
            const bool stalled = std::uint16_t(velocity) < value;
            require(FM::belowStall(speed, threshold) == stalled &&
                    FM::aboveStall(speed, threshold) == (std::uint16_t(velocity) > value),
                    "stall strict unsigned comparisons changed");
            for (int hz : {4, 15, 120}) for (auto severity : {StallSeverity::Normal, StallSeverity::Severe}) {
                const int shift = severity == StallSeverity::Severe ? 1 : 2;
                const auto response = FM::stallResponse(speed, threshold, severity, ControlBoundary<F>::frequency(hz));
                require(response.stalled == stalled && legacy::signedAngle(response.noseDrop) ==
                        (stalled ? (value - std::uint16_t(velocity)) >> shift : 0),
                        "fixed stall drop gained frequency scaling or changed rounding");
            }
        }
        const auto equal = FC::speed(value);
        require(!FM::belowStall(equal, threshold) && !FM::aboveStall(equal, threshold), "stall equality is not strict");
        const auto response = FM::stallResponse(equal, threshold, StallSeverity::Normal, ControlBoundary<F>::frequency(15));
        require(!response.stalled && response.noseDrop == Angle<F>{}, "stall at exact threshold");
    }
    rejects([] { FM::stallResponse({}, {}, static_cast<StallSeverity>(99), ControlBoundary<F>::frequency(15)); });
    // Freeze the stall-warning policy from 02e55d1: signed pitch below the
    // horizon, or the unsigned scene-height word below the 200-unit floor.
    for (int height = 0; height < 65536; ++height)
    for (int pitch : {-32768, -2, -1, 0, 1, 2, 32767})
        require(FM::stallWarning(legacy::angleFromWord(pitch), FA::render(static_cast<std::int16_t>(height))) ==
                (pitch < 0 || std::uint16_t(height) < 200),
                "fixed stall-warning policy changed");
}
void productionCaller() {
    for (int height = -32768; height <= 32767; ++height)
    for (int pitch : {-32768, -1, 0, 1, 32767}) {
        g_viewZ = height;
        g_ourPitch = legacy::angleFromWord(pitch);
        require(flightStallWarningRequired() == (pitch < 0 || std::uint16_t(height) < 200),
                "production stall warning differs from original pitch/height policy");
    }
    Game data{};
    auto *saved = gameData;
    gameData = &data;
    std::uint32_t random = 73;
    auto next = [&] { random = random * 1664525u + 1013904223u; return random; };
    for (int tick = 0; tick < 24000; ++tick) {
        const int pitch = word(next()), threshold = word(next());
        const int speed = int(next() % 262144) - 131072;
        const int ground = word(next()), height = word(next());
        g_ourPitch = legacy::angleFromWord(pitch);
        g_velocity = FC::speed(speed);
        g_stallSpeed = legacy::stallFromUnits(threshold);
        g_groundAltitude = ground;
        g_viewZ = height;
        g_orientationDirty = tick % 3;
        g_frameRateScaling = 1 + next() % 120;
        data.unk4 = next() % 4;
        g_gunHits = next() % 20;
        const bool stalled = std::uint16_t(threshold) > std::uint16_t(speed) && std::uint16_t(ground) < std::uint16_t(height);
        const int shift = data.unk4 == 2 || g_gunHits > 8 ? 1 : 2;
        const int expectedPitch = stalled ? word(pitch - ((std::uint16_t(threshold) - std::uint16_t(speed)) >> shift)) : pitch;
        require(correctFlightStall() == stalled && legacy::signedAngle(g_ourPitch) == expectedPitch &&
                g_orientationDirty == (stalled ? 1 : tick % 3), "production stall correction differs from frozen baseline");
    }
    data.unk4 = 0;
    g_gunHits = 0;
    g_groundAltitude = 0;
    g_viewZ = 1;
    g_stallSpeed = legacy::stallFromUnits(2);
    g_velocity = FC::speed(1);
    g_ourPitch = legacy::angleFromWord(100);
    g_orientationDirty = 0;
    require(correctFlightStall() && legacy::signedAngle(g_ourPitch) == 100 && g_orientationDirty == 1,
            "zero rounded drop must still mark a stall correction");
    gameData = nullptr;
    g_groundAltitude = g_viewZ;
    require(!correctFlightStall(), "grounded aircraft must not need stall severity state");
    gameData = saved;
}
void modernMath() {
    constexpr double radiansPerWord = 6.28318530717958647692 / 65536;
    const auto speed = MC::speed(1000.125), faster = MC::speed(1001.25);
    const auto threshold = MC::stall(1000.25);
    require(MM::belowStall(speed, threshold) && MM::aboveStall(faster, threshold), "modern stall threshold quantizes");
    require(!MM::belowStall(MC::speed(1000.25), threshold) && !MM::aboveStall(MC::speed(1000.25), threshold),
            "modern equality is not strict");
    require(MM::aboveStall(MC::speed(65536.5), MC::stall(1000)), "modern speed comparison wraps at a word");
    for (auto severity : {StallSeverity::Normal, StallSeverity::Severe}) {
        const int divisor = severity == StallSeverity::Severe ? 2 : 4;
        const double expected = 0.125 / divisor * radiansPerWord * 15 * 100;
        for (int hz : {15, 30, 60, 120}) {
            Angle<M> pitch;
            for (int tick = 0; tick < hz * 100; ++tick) {
                const auto response = MM::stallResponse(speed, threshold, severity, ControlBoundary<M>::frequency(hz));
                require(response.stalled, "modern subunit deficit was lost");
                pitch -= response.noseDrop;
            }
            require(std::abs(Boundary<M>::radians(pitch) + expected) < 1e-12, "modern stall depends on tick frequency");
        }
    }
    const auto safe = MM::stallResponse(faster, threshold, StallSeverity::Normal, ControlBoundary<M>::frequency(15));
    require(!safe.stalled && safe.noseDrop == Angle<M>{}, "modern nose drops above stall speed");
    rejects([] { MC::stall(std::numeric_limits<double>::infinity()); });
    rejects([] { MM::stallResponse(MC::speed(-1e308), MC::stall(1e308), StallSeverity::Normal, ControlBoundary<M>::frequency(15)); });
    rejects([] { MM::stallResponse({}, {}, static_cast<StallSeverity>(99), ControlBoundary<M>::frequency(15)); });
    // Modern stall warning: fractional pitch sign and unwrapped scene height.
    for (double height : {-0.5, 0.0, 100.0, 199.5, 199.999, 200.0, 200.5,
                          32767.5, 32768.0, 65535.5, 65536.0, 1e6})
    for (double pitch : {-1.0, -1e-5, -1e-9, 0.0, 1e-9, 1e-5, 1.0})
        require(MM::stallWarning(MB::radians(pitch), MA::render(height)) ==
                (pitch < 0 || (height >= 0 && height < 200)),
                "modern stall warning quantized pitch or wrapped height");
}
}
int main() { fixedMath(); productionCaller(); modernMath(); }
