#include "math/legacy_horizontal.hpp"
#include "math/legacy_map.hpp"
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
using FC = HorizontalBoundary<F>;
using MC = HorizontalBoundary<M>;
using FM = HorizontalMath<F>;
using MM = HorizontalMath<M>;
void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
template<class Fn> void rejects(Fn fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception &) { threw = true; }
    require(threw, "invalid horizontal operation accepted");
}
// Frozen expressions from egflight/egframe/egsys at 6bc4cc2. Boundary cases
// spell out signed 32-bit wrapping rather than executing the old C++ overflow.
std::int32_t dword(std::int64_t value) {
    auto v = value % 4294967296LL;
    if (v > INT32_MAX) v -= 4294967296LL;
    if (v < INT32_MIN) v += 4294967296LL;
    return static_cast<std::int32_t>(v);
}
int step(int speed, int direction, int hz) {
    const auto product = std::int64_t(speed) * direction;
    return (rotation_reference::floorDivide(product, 32768) +
            (rotation_reference::floorDivide(product, 16384) & 1)) / 10 / hz;
}

void fixedMath() {
    const RotationMath<F> math(g_angleLut);
    for (int angle = 0; angle < 65536; ++angle) {
        const auto a = Boundary<F>::angleWord(angle);
        for (int speed : {-32768, -32767, -100, -1, 0, 1, 100, 32767})
            for (int hz : {1, 3, 4, 60, 120}) {
                const auto delta = FM::increments(FC::speed(speed), math.sine(a), math.cosine(a),
                                                  ControlBoundary<F>::frequency(hz));
                require(FC::displacement(delta.x) == step(speed, rotation_reference::sine(angle, g_angleLut), hz) &&
                        FC::displacement(delta.y) == step(speed, rotation_reference::sine(angle + 16384, g_angleLut), hz),
                        "fixed horizontal integration changed");
            }
    }
    for (std::int32_t a : {INT32_MIN, -100000, -1, 0, 1, 100000, INT32_MAX})
        for (std::int32_t b : {INT32_MIN, -100000, -1, 0, 1, 100000, INT32_MAX}) {
            const auto x = FC::coordinate<ViewXAxis>(a), y = FC::coordinate<ViewXAxis>(b);
            require(FC::coordinate(x + FC::displacement<ViewXAxis>(b)) == dword(std::int64_t(a) + b),
                    "coordinate addition width changed");
            require(FC::coordinate(x - FC::displacement<ViewXAxis>(b)) == dword(std::int64_t(a) - b),
                    "coordinate subtraction width changed");
            for (int divisor : {1, 2, 3, 7, 14}) {
                const auto expected = dword(std::int64_t(a) - dword(std::int64_t(a) - b) / divisor);
                require(FC::coordinate(FM::approach(x, y, divisor)) == expected, "landing XY easing changed");
            }
            for (int n = 0; n <= 19; ++n) {
                const auto expected = dword(std::int64_t(a) + std::int64_t(dword(std::int64_t(b) - a)) * n / 19);
                require(FC::coordinate(FM::interpolate(x, y, FrameFraction::fromTicks(n, 19))) == expected,
                        "horizontal interpolation changed");
            }
        }
    rejects([] { FM::approach(legacy::viewX(0), legacy::viewX(10), 0); });
    rejects([] {
        FM::interpolate(legacy::viewX(0), legacy::viewX(INT32_MAX),
                        FrameFraction::fromTicks(INT64_MAX / 32768, INT64_MAX / 32768));
    });
    // Coarse map units keep the (fine + 0x10) >> 5 quantization; Y mirrors
    // against 0x8000. Both original egframe spellings agree at every input.
    for (std::int32_t fine : {-65536, -33, -17, -16, -1, 0, 1, 15, 16, 17, 31, 32,
                              0x1000, 0x80000, 0xFFFFF, 0x100000, 0x10000F}) {
        require(FM::mapUnitsX(FC::coordinate<ViewXAxis>(fine)) ==
                static_cast<std::int16_t>((fine + 0x10) >> 5), "map X quantization changed");
        require(FM::mapUnitsY(FC::coordinate<ViewYAxis>(fine)) ==
                static_cast<std::int16_t>(0x8000 - ((fine + 0x10) >> 5)), "map Y mirror changed");
        const auto pos = MapBoundary<F>::position(FC::coordinate<ViewXAxis>(fine),
                                                  FC::coordinate<ViewYAxis>(fine));
        require(MapBoundary<F>::wordX(pos) == static_cast<std::int16_t>((fine + 0x10) >> 5) &&
                MapBoundary<F>::wordY(pos) == static_cast<std::int16_t>(0x8000 - ((fine + 0x10) >> 5)),
                "map word extraction changed");
    }
    // Map interpolation reproduces the lerpLinear word math per axis.
    for (std::int16_t a : {-32768, -1, 0, 1, 100, 32767})
        for (std::int16_t b : {-32768, -1, 0, 1, 100, 32767})
            for (auto tick : {std::pair{0, 1}, {1, 4}, {3, 4}, {14, 15}}) {
                const auto pos = MapMath<F>::interpolate(MapBoundary<F>::position(a, a),
                                                         MapBoundary<F>::position(b, b),
                                                         FrameFraction::fromTicks(tick.first, tick.second));
                require(MapBoundary<F>::wordX(pos) ==
                        static_cast<std::int16_t>(a + static_cast<std::int32_t>(
                            std::int64_t(b - a) * tick.first / tick.second)),
                        "map interpolation differs from lerpLinear");
            }
    require(MapBoundary<F>::position(1, 2) == MapBoundary<F>::position(1, 2) &&
            MapBoundary<F>::position(1, 2) != MapBoundary<F>::position(2, 1),
            "map position equality changed");
}

void productionCaller() {
    std::int32_t x = INT32_MAX - 10, y = INT32_MIN + 10;
    g_ViewX = legacy::viewX(x); g_ViewY = legacy::viewY(y);
    for (int tick = 0; tick < 24000; ++tick) {
        const auto angle = static_cast<std::uint16_t>(tick * 73);
        const auto speed = rotation_reference::word(tick * 31);
        g_ourHead = Boundary<F>::angleWord(angle);
        g_frameRateScaling = 1 + tick % 120;
        g_autoLandingActive = tick % 7 == 0;
        if (!g_autoLandingActive) {
            x = dword(std::int64_t(x) + step(speed, rotation_reference::sine(angle, g_angleLut), g_frameRateScaling));
            y = dword(std::int64_t(y) + step(speed, rotation_reference::sine(angle + 16384, g_angleLut), g_frameRateScaling));
        }
        advanceFlightHorizontal(FC::speed(speed));
        require(FC::coordinate(g_ViewX) == x && FC::coordinate(g_ViewY) == y,
                "persistent horizontal caller differs from frozen baseline");
    }
    g_autoLandingActive = 1; g_frameRateScaling = 0;
    advanceFlightHorizontal(FC::speed(123));
    require(FC::coordinate(g_ViewX) == x && FC::coordinate(g_ViewY) == y, "automatic landing moved aircraft twice");
}

void modernMath() {
    const RotationMath<M> math;
    const auto heading = Boundary<M>::radians(0.321);
    const auto delta = MM::increments(MC::speed(0.125), math.sine(heading), math.cosine(heading),
                                      ControlBoundary<M>::frequency(120));
    auto x = MC::coordinate<ViewXAxis>(1000.125);
    auto y = MC::coordinate<ViewYAxis>(-1000.25);
    for (int tick = 0; tick < 12000; ++tick) { x += delta.x; y += delta.y; }
    require(std::abs(MC::coordinate(x) - (1000.125 + 1.25 * std::sin(0.321))) < 1e-8 &&
            std::abs(MC::coordinate(y) - (-1000.25 + 1.25 * std::cos(0.321))) < 1e-8,
            "modern movement lost fractions");
    const auto midpoint = MM::interpolate(MC::coordinate<ViewXAxis>(1.125), MC::coordinate<ViewXAxis>(1.375),
                                          FrameFraction::fromTicks(1, 2));
    require(MC::coordinate(midpoint) == 1.25, "modern snapshot interpolation quantized");
    const auto approach = MM::approach(MC::coordinate<ViewYAxis>(1.25), MC::coordinate<ViewYAxis>(0.25), 4);
    require(MC::coordinate(approach) == 1, "modern approach quantized");
    const auto wide = MC::coordinate<ViewXAxis>(INT32_MAX) + MC::displacement<ViewXAxis>(1.25);
    require(MC::coordinate(wide) == double(INT32_MAX) + 1.25, "modern coordinate inherited fixed wrapping");
    rejects([] { MC::coordinate<ViewXAxis>(std::numeric_limits<double>::infinity()); });
    rejects([] { MC::speed(std::numeric_limits<double>::quiet_NaN()); });
    rejects([] {
        const auto huge = std::numeric_limits<double>::max();
        (void)(MC::coordinate<ViewYAxis>(huge) + MC::displacement<ViewYAxis>(huge));
    });
    // Modern map units keep the fraction instead of the quantized word.
    require(MM::mapUnitsX(MC::coordinate<ViewXAxis>(1000.5)) == (1000.5 + 16.0) / 32.0,
            "modern map X units quantized");
    require(MM::mapUnitsY(MC::coordinate<ViewYAxis>(1000.5)) == 32768.0 - (1000.5 + 16.0) / 32.0,
            "modern map Y mirror quantized");
    // The legacy word drops the fraction and wraps modulo 2^16.
    require(MapBoundary<M>::wordX(MapBoundary<M>::position(31.75, 0.0)) == 31,
            "modern map word kept the fraction");
    require(MapBoundary<M>::wordX(MapBoundary<M>::position(65536.5, 0.0)) == 0 &&
            MapBoundary<M>::wordX(MapBoundary<M>::position(-1.5, 0.0)) == -1,
            "modern map word wrap changed");
    const auto mid = MapMath<M>::interpolate(MapBoundary<M>::position(1.0, 2.0),
                                             MapBoundary<M>::position(3.0, 4.0),
                                             FrameFraction::fromTicks(1, 2));
    require(MapBoundary<M>::x(mid) == 2.0 && MapBoundary<M>::y(mid) == 3.0,
            "modern map interpolation quantized");
    rejects([] { MapBoundary<M>::position(1.0, std::numeric_limits<double>::infinity()); });
}
}
int main() {
    fixedMath(); productionCaller(); modernMath();
    std::puts("typed horizontal tests passed");
}
