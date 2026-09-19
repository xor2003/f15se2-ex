#include "math/guidance.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_flight_control.hpp"
#include "math_rotation_reference.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
void fixedCases() {
    using rotation_reference::word;
    using rotation_reference::floorDivide;
    for (int raw = 0; raw < 65536; ++raw)
    for (int bearing : {-32768, -5121, 0, 5121, 32767})
    for (int target : {-32768, 1, 8192, 32767}) {
        const int head = word(raw), pitch = word(raw * 3), roll = word(raw * 7);
        const int offset = (raw & 15) * 256 - 2048;
        const int trim = word(raw * 11), height = word(raw * 13);
        const int bank = std::clamp(int(word(offset - head + bearing)), -5120, 5120) * 2;
        const int expectedRoll = -std::clamp(int(floorDivide(word(bank - roll), 64)), -24, 24);
        const int desiredPitch = std::clamp((target - height) * 16 - trim, -5120, 3072);
        const int expectedPitch = std::clamp(int(floorDivide(desiredPitch - pitch, 128)), -8, 8);
        const auto actual = GuidanceMath<F>::altitudeHold(legacy::renderHeightFromUnits(target),
            legacy::renderHeightFromUnits(height), legacy::angles(head, pitch, roll),
            legacy::angleFromWord(bearing), legacy::angleFromWord(offset), legacy::angleFromWord(trim));
        require(ControlBoundary<F>::roll(actual.roll) == expectedRoll &&
            ControlBoundary<F>::pitch(actual.pitch) == expectedPitch, "fixed altitude-hold command differs from baseline");
    }
}
void modernCases() {
    constexpr double wordRadians = 6.28318530717958647692 / 65536;
    for (double delta : {-0.5, -0.01, 0.0, 0.01, 0.5}) {
        const auto result = GuidanceMath<M>::altitudeHold(AltitudeBoundary<M>::render(1000 + delta),
            AltitudeBoundary<M>::render(1000), {}, Boundary<M>::radians(delta * wordRadians), {}, {});
        require(std::abs(ControlBoundary<M>::radiansPerSecond(result.roll) + delta * 4 * wordRadians) < 1e-14,
            "modern roll command lost fractional bearing");
        require(std::abs(ControlBoundary<M>::radiansPerSecond(result.pitch) - delta * 16 * wordRadians) < 1e-14,
            "modern pitch command lost fractional height");
    }
    const auto capped = GuidanceMath<M>::altitudeHold(AltitudeBoundary<M>::render(60000), {}, {},
        Boundary<M>::radians(1), {}, {});
    require(std::abs(ControlBoundary<M>::radiansPerSecond(capped.roll) + 24 * 128 * wordRadians) < 1e-14 &&
        std::abs(ControlBoundary<M>::radiansPerSecond(capped.pitch) - 8 * 128 * wordRadians) < 1e-14,
        "modern command limits changed");
    constexpr double pi = 3.14159265358979323846;
    const auto wrapped = GuidanceMath<M>::altitudeHold({}, {}, {Boundary<M>::radians(pi - 0.00001), {}, {}},
        Boundary<M>::radians(-pi + 0.00001), {}, {});
    require(std::abs(ControlBoundary<M>::radiansPerSecond(wrapped.roll) + 0.00008) < 1e-14,
        "modern bearing takes the long path across the wrap");
}
}
int main() { fixedCases(); modernCases(); }
