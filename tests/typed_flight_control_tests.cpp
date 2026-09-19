#include "math/legacy_flight_control.hpp"
#include "math/legacy_rotation.hpp"
#include "math_attitude_reference.hpp"
#include "egdata.h"
#include "egflight.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
using C = ControlBoundary<F>;
using D = ControlBoundary<M>;
using A = Boundary<F>;
using B = Boundary<M>;
using FM = FlightControlMath<F>;
using MM = FlightControlMath<M>;
using rotation_reference::word;
constexpr double wordRadians = 6.28318530717958647692 / 65536;

void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
template<class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (const std::exception &) { rejected = true; }
    require(rejected, "invalid control input was accepted");
}

// Frozen control expressions from egflight.c at 66ce785. Multiplication and
// explicit signed decoding replace undefined negative shifts in the oracle.
std::int16_t rollIncrement(std::int32_t command, int hz) {
    auto scaled = (static_cast<std::int64_t>(command) * 128) % 4294967296LL;
    if (scaled < -2147483648LL) scaled += 4294967296LL;
    if (scaled > 2147483647LL) scaled -= 4294967296LL;
    return word(scaled / hz);
}
void fixedInputs() {
    for (int x = 0; x < 256; ++x) for (int y = 0; y < 256; ++y) {
        int roll = x / 16 - 8, pitch = y / 16 - 8;
        if (roll < 0) ++roll;
        if (pitch < 0) ++pitch;
        roll = -((std::abs(roll) + 2) * roll) * 2;
        pitch *= 6;
        if (pitch < 0) pitch /= 2;
        const auto c = FM::fromJoystick(C::joystick(x, y));
        require(C::roll(c.roll) == roll && C::pitch(c.pitch) == pitch, "joystick response changed");
        const auto m = MM::fromJoystick(D::joystick(x, y));
        require(std::abs(D::radiansPerSecond(m.roll) - roll * 128 * wordRadians) < 1e-14 &&
                std::abs(D::radiansPerSecond(m.pitch) - pitch * 128 * wordRadians) < 1e-14,
                "modern joystick response differs");
    }
    for (const int hz : {1, 2, 3, 4, 7, 16, 60, 120, 32767}) {
        for (int v = -32768; v <= 32767; ++v) {
            const auto d = FM::increments(C::roll(v), C::pitch(v), C::yaw(v), C::frequency(hz));
            require(legacy::signedAngle(d.roll) == rollIncrement(v, hz), "roll increment changed");
            require(legacy::signedAngle(d.pitch) == word(v * 128) / hz, "pitch predivision narrowing changed");
            require(legacy::signedAngle(d.yaw) == v / hz, "yaw truncation changed");
            require(C::yaw(FM::groundYaw(C::roll(v))) == word(-static_cast<std::int64_t>(v) * 64),
                    "ground steering changed");
        }
        for (const std::int32_t v : {INT32_MIN, INT32_MIN + 1, -16777217, -16777216,
                                     16777215, 16777216, INT32_MAX - 1, INT32_MAX}) {
            const auto d = FM::increments(C::roll(v), {}, {}, C::frequency(hz));
            require(legacy::signedAngle(d.roll) == rollIncrement(v, hz), "32-bit roll scaling changed");
            require(C::yaw(FM::groundYaw(C::roll(v))) == word(-static_cast<std::int64_t>(v) * 64),
                    "wide ground steering changed");
        }
    }
    require(C::roll(C::roll(INT32_MAX) + C::roll(1)) == INT32_MIN, "roll command store width changed");
    require(C::pitch(C::pitch(32767) + C::pitch(1)) == -32768, "pitch command store width changed");
    require(C::yaw(C::yaw(-32768) + C::yaw(-1)) == 32767, "yaw command store width changed");
    auto roll = C::roll(126);
    auto pitch = C::pitch(-21);
    const int result = legacy::updateControlFromWords(roll, pitch, [](int *r, std::int16_t *p) {
        require(*r == 126 && *p == -21, "platform adapter lost inputs");
        *r = -77777; *p = 1234; return 7;
    });
    require(result == 7 && C::roll(roll) == -77777 && C::pitch(pitch) == 1234,
            "platform adapter lost results");
}

void productionCaller() {
    // Verify multiplication side/order, zero-step skipping, recovery and refresh
    // accounting through the same function called by stepFlightModel.
    for (int tick = 0; tick < 4096; ++tick) {
        auto expected = rotation_reference::rotation(tick * 7, tick * 3, -tick * 5, g_angleLut);
        g_orientMatrix = A::matrixWords(expected.data());
        g_matrixScratch = Matrix3<F>::identity();
        auto scratch = rotation_reference::Matrix{32767,0,0,0,32767,0,0,0,32767};
        g_rotationCounter = tick & 7;
        g_orientationDirty = tick % 5 == 0;
        g_rollWasNonzero = tick % 2;
        int count = g_rotationCounter;
        bool dirty = g_orientationDirty != 0;
        const int roll = tick % 9 == 0 ? 0 : tick % 253 - 126;
        const int pitch = tick % 7 == 0 ? 0 : tick % 1024 - 512;
        const auto yaw = word(tick * 31);
        const int hz = 1 + tick % 120;
        const int r = rollIncrement(roll, hz), p = word(pitch * 128) / hz, y = yaw / hz;
        auto apply = [&](int angle, int axis) {
            if (!angle) return;
            const auto s = rotation_reference::sine(angle, g_angleLut);
            const auto c = rotation_reference::sine(angle + 16384, g_angleLut);
            rotation_reference::Matrix delta;
            if (axis == 0) delta = {c,s,0,word(-s),c,0,0,0,32767};
            else if (axis == 1) delta = {32767,0,0,0,c,word(-s),0,s,c};
            else delta = {c,0,s,0,32767,0,word(-s),0,c};
            expected = axis == 2 ? rotation_reference::multiply(delta, expected)
                                 : rotation_reference::multiply(expected, delta);
            scratch = expected;
            if (!(++count & 7)) dirty = true;
        };
        apply(r, 0); apply(p, 1); apply(y, 2);
        const auto recovered = rotation_reference::recover(expected, g_rollWasNonzero != 0, g_angleLut);
        const auto deltas = FM::increments(C::roll(roll), C::pitch(pitch), C::yaw(yaw), C::frequency(hz));
        advanceFlightOrientation(deltas);
        rotation_reference::Matrix actual, actualScratch;
        A::matrixWords(g_orientMatrix, actual.data());
        A::matrixWords(g_matrixScratch, actualScratch.data());
        require(actual == expected && actualScratch == scratch, "flight rotation order changed");
        require(g_rotationCounter == count && (g_orientationDirty != 0) == (dirty || recovered.dirty),
                "flight rotation refresh changed");
        require(legacy::signedAngle(g_ourHead) == recovered.yaw &&
                legacy::signedAngle(g_ourPitch) == recovered.pitch &&
                legacy::signedAngle(g_ourRoll) == recovered.roll, "flight recovery changed");
    }
}

void modernPrecision() {
    const auto rate = D::radiansPerSecond<YawAxis>(wordRadians / 8);
    Angle<M> accumulated;
    for (int tick = 0; tick < 12000; ++tick)
        accumulated += MM::increments({}, {}, rate, D::frequency(120)).yaw;
    require(std::abs(B::radians(accumulated) - 100 * wordRadians / 8) < 1e-14,
            "modern control integration lost fractional angle words");
    const auto fine = MM::increments(D::radiansPerSecond<RollAxis>(0.3),
                                    D::radiansPerSecond<PitchAxis>(-0.7), rate, D::seconds(0.25));
    require(std::abs(B::radians(fine.roll) - 0.075) < 1e-15 &&
            std::abs(B::radians(fine.pitch) + 0.175) < 1e-15, "modern step integration changed units");
    require(D::radiansPerSecond(MM::groundYaw(D::radiansPerSecond<RollAxis>(0.3))) == -0.15,
            "modern ground steering changed units");
    const auto fixed = FM::increments({}, C::pitch(256), {}, C::frequency(4));
    const auto modern = MM::increments({}, D::radiansPerSecond<PitchAxis>(256 * 128 * wordRadians),
                                      {}, D::frequency(4));
    require(legacy::signedAngle(fixed.pitch) == -8192 && B::radians(modern.pitch) > 0,
            "modern controls incorrectly inherited fixed word overflow");
    rejects([] { C::frequency(0); });
    rejects([] { C::frequency(-1); });
    rejects([] { D::frequency(0); });
    for (double invalid : {0.0, -1.0, 1.1, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { D::seconds(invalid); });
    rejects([] { D::radiansPerSecond<RollAxis>(std::numeric_limits<double>::infinity()); });
    rejects([] { D::radiansPerSecond<PitchAxis>(std::numeric_limits<double>::quiet_NaN()); });
    rejects([] {
        const auto huge = D::radiansPerSecond<RollAxis>(std::numeric_limits<double>::max());
        (void)(huge + huge);
    });
}
}
void analogPrecision() {
    const AnalogResponse profile{D::radiansPerSecond<RollAxis>(2), D::radiansPerSecond<PitchAxis>(1),
        D::radiansPerSecond<PitchAxis>(0.5), 0.1};
    double previous = -3;
    for (int i = -32768; i <= 32767; ++i) {
        const double input = i < 0 ? i / 32768.0 : i / 32767.0;
        const auto commands = MM::fromAnalog(D::analogStick(input, input), profile);
        const double shaped = std::abs(input) <= 0.1 ? 0 : std::copysign((std::abs(input) - 0.1) / 0.9, input);
        const double roll = D::radiansPerSecond(commands.roll);
        require(std::abs(roll - shaped * 2) < 1e-14 &&
            std::abs(D::radiansPerSecond(commands.pitch) - shaped * (input < 0 ? 0.5 : 1)) < 1e-14,
            "analog response lost precision or pitch asymmetry");
        require(roll >= previous && (std::abs(input) <= 0.1 || roll > previous), "analog response is not monotonic");
        previous = roll;
    }
    rejects([&] { MM::fromAnalog(D::analogStick(0, 0), {profile.maximumRoll,
        profile.maximumPositivePitch, profile.maximumNegativePitch, 1}); });
    rejects([] { D::analogStick(1.01, 0); });
    rejects([] { D::analogStick(0, std::numeric_limits<double>::quiet_NaN()); });
}
int main() {
    fixedInputs(); productionCaller(); modernPrecision();
    analogPrecision();
    std::puts("typed flight control tests passed");
}
