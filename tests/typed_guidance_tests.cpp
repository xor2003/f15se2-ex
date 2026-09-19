#include "math/guidance.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_flight_control.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_propulsion.hpp"
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
void recoveryCases() {
    using rotation_reference::word;
    using rotation_reference::floorDivide;
    for (int raw = 0; raw < 65536; ++raw)
    for (int knots : {0, 15, 16, 160, 349, 350, 800, 32767}) {
        const int heading = word(raw * 7);
        const int limit = knots / 16 * 256;
        const auto bank = GuidanceMath<F>::recoveryBank(legacy::angleFromWord(raw),
            legacy::angleFromWord(heading), legacy::speedFromUnits(knots * 27), false);
        require(legacy::signedAngle(bank) == word(std::clamp(int(word(raw - heading)), -limit, limit) * 2),
            "recovery bank speed limit differs from scalar formula");
        require(legacy::signedAngle(GuidanceMath<F>::recoveryBank(legacy::angleFromWord(raw),
            legacy::angleFromWord(heading), legacy::speedFromUnits(knots * 27), true)) == 0,
            "recovery corridor must demand level bank");
    }
    for (int raw = 0; raw < 65536; ++raw)
    for (int target : {-20, 50, 4096, 4196}) {
        const int roll = word(raw), pitch = word(raw * 3), trim = word(raw * 7);
        const int bank = word(raw * 11), height = word(raw * 13);
        const auto thrust = GuidanceMath<F>::recoveryThrust(legacy::angleFromWord(bank),
            legacy::renderHeightFromUnits(target));
        require(legacy::thrustUnits(thrust) == std::clamp(std::abs(bank) / 256 + target / 64, 35, 80),
            "fixed recovery thrust differs from scalar formula");
        const auto result = GuidanceMath<F>::recoveryAttitude(legacy::renderHeightFromUnits(target),
            legacy::renderHeightFromUnits(height), legacy::angles(0, pitch, roll),
            legacy::angleFromWord(bank), legacy::angleFromWord(trim));
        const int expectedRoll = -std::clamp(int(floorDivide(word(bank - roll), 64)), -32, 32);
        const int expectedPitch = std::clamp(std::clamp(int(floorDivide(target - height, 8) +
            floorDivide(trim, 128)), -24, 24) - int(floorDivide(pitch, 128)), -16, 16);
        require(ControlBoundary<F>::roll(result.roll) == expectedRoll &&
            ControlBoundary<F>::pitch(result.pitch) == expectedPitch, "fixed recovery steering differs from scalar formula");
    }
    constexpr double unit = 6.28318530717958647692 / 65536;
    const auto fractionalThrust = GuidanceMath<M>::recoveryThrust(Boundary<M>::radians(256.5 * unit),
        AltitudeBoundary<M>::render(2560.5));
    require(std::abs(PropulsionBoundary<M>::thrust(fractionalThrust) - (256.5 / 256 + 2560.5 / 64)) < 1e-14,
        "modern recovery thrust lost fractional input");
    for (double knots : {0.25, 15.5, 16.25, 350.5}) {
        const auto bank = GuidanceMath<M>::recoveryBank(Boundary<M>::radians(1), {},
            AirspeedBoundary<M>::speed(knots * 27), false);
        require(std::abs(Boundary<M>::radians(bank) - std::min(1.0, knots / 16 * 256 * unit) * 2) < 1e-14,
            "modern recovery bank quantized indicated speed");
    }
    for (double delta : {-0.5, -0.01, 0.0, 0.01, 0.5}) {
        const auto result = GuidanceMath<M>::recoveryAttitude(AltitudeBoundary<M>::render(1000 + delta),
            AltitudeBoundary<M>::render(1000), {}, Boundary<M>::radians(delta * unit), {});
        require(std::abs(ControlBoundary<M>::radiansPerSecond(result.roll) + delta * 2 * unit) < 1e-14 &&
            std::abs(ControlBoundary<M>::radiansPerSecond(result.pitch) - delta * 16 * unit) < 1e-14,
            "modern recovery steering lost fractional input");
    }
}
}
int main() { fixedCases(); modernCases(); recoveryCases(); }
