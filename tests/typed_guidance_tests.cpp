#include "math/guidance.hpp"
#include "math/legacy_altitude.hpp"
#include "math/legacy_rotation.hpp"
#include "math/legacy_flight_control.hpp"
#include "math/legacy_airspeed.hpp"
#include "math/legacy_propulsion.hpp"
#include "math/legacy_map.hpp"
#include "math/legacy_horizontal.hpp"
#include "math_rotation_reference.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

/* Real egmath.c word functions, linked from f15se2_core: the fixed oracles the
 * typed guidance helpers must reproduce bit-exactly. */
extern int16_t computeBearing(int16_t deltaX, int16_t deltaY);
extern int16_t clampRange(int16_t value, int16_t minVal, int16_t maxVal);
extern int16_t sine(int16_t angle);
extern int16_t cosine(int16_t angle);
extern int16_t sinMul(int16_t angle, int16_t value);
extern int16_t cosMul(int16_t angle, int16_t value);
extern const int16_t g_angleLut[260];

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
static_assert(!std::is_constructible_v<MapPosition<F>, int, int>);
static_assert(!std::is_constructible_v<MapPosition<M>, double, double>);
static_assert(!std::is_convertible_v<MapPosition<F>, MapPosition<M>>);
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
void modernApproachCases() {
    const auto approach = [](double origin, double x, double y) {
        return GuidanceMath<M>::recoveryApproach(MapBoundary<M>::position(origin + x, origin + y),
            MapBoundary<M>::position(origin, origin), {}, true, RecoveryDirection::North, false);
    };
    for (double origin : {0.0, 1000000.0, -1000000.0}) {
        const auto result = approach(origin, 10.25, 20.5);
        require(std::abs(Boundary<M>::radians(result.bearing) - std::atan2(30.75, -89.5)) < 1e-14,
            "modern approach lost fractional coordinates or translation invariance");
        require(AltitudeBoundary<M>::render(result.height) == 221.5 && result.exitSlowMotion && result.allowBrakes,
            "modern approach height or policy changed");
    }
    const auto large = approach(0, 16000, 16000);
    require(std::abs(Boundary<M>::radians(large.bearing) - std::atan2(48000.0, -19100.0)) < 1e-14 &&
        AltitudeBoundary<M>::render(large.height) == 4196 && !large.exitSlowMotion,
        "modern approach retained signed-word range loss");
    const auto corridor = GuidanceMath<M>::recoveryApproach({}, {}, {}, true, RecoveryDirection::North, true);
    require(AltitudeBoundary<M>::render(corridor.height) == -20, "modern corridor descent target changed");
}

/* Projectile guidance state: the updateThreatTargeting steering chain migrated
 * off raw angle/fine words. Fixed oracles are the real egmath.c functions;
 * modern cases prove the fractions the word path discarded. */
void projectileFixedCases() {
    using rotation_reference::word;
    for (int raw = 0; raw < 65536; ++raw)
        for (int dy : {-32768, -12345, -4096, -1, 0, 1, 4096, 12345, 32767}) {
            const int dx = word(raw);
            require(legacy::signedAngle(GuidanceMath<F>::aimBearing(dx, dy)) ==
                    computeBearing(static_cast<int16_t>(dx), static_cast<int16_t>(dy)),
                "fixed aim bearing differs from computeBearing");
        }
    for (int dx : {-32768, -1, 0, 1, 12345})
        for (int raw = 0; raw < 65536; ++raw)
            require(legacy::signedAngle(GuidanceMath<F>::aimBearing(dx, word(raw))) ==
                    computeBearing(static_cast<int16_t>(dx), static_cast<int16_t>(word(raw))),
                "fixed aim bearing differs from computeBearing (dy sweep)");
    /* clampRange quirk preserved: the <= -0x4000 floor picks the high end. */
    for (int raw = 0; raw < 65536; ++raw)
        for (int limit : {32, 256, 2048, 8192, 30000})
            require(legacy::signedAngle(GuidanceMath<F>::limitTurn(legacy::angleFromWord(raw),
                        -limit, limit)) == clampRange(static_cast<int16_t>(word(raw)),
                        static_cast<int16_t>(-limit), static_cast<int16_t>(limit)),
                "fixed turn clamp differs from clampRange");
    /* Steering step: (delta << 2) / scaling, word-truncated int division. */
    for (int raw = 0; raw < 65536; ++raw)
        for (int scaling : {1, 4, 15, 60})
            require(legacy::signedAngle(GuidanceMath<F>::turnStep(legacy::angleFromWord(raw),
                        scaling)) == static_cast<int16_t>((word(raw) << 2) / scaling),
                "fixed turn step differs from the word formula");
    /* Bank = delta << 1 in word space. */
    for (int raw = 0; raw < 65536; ++raw)
        require(legacy::signedAngle(GuidanceMath<F>::bankFromTurn(legacy::angleFromWord(raw))) ==
                static_cast<int16_t>(word(raw) << 1), "fixed bank step differs from the word formula");
    /* Scaled decrement: (magnitude << shift) / divisor used by gravity/dive. */
    for (int mag : {-4096, -2048, 0, 2048, 4096})
        for (int scaling : {1, 4, 15, 60})
            require(legacy::signedAngle(GuidanceMath<F>::scaledStep(mag, scaling)) ==
                    static_cast<int16_t>(mag / scaling),
                "fixed scaled step differs from the word formula");
    /* Fine displacement: ((int64)sine(heading) * step) >> 15. */
    for (int raw = 0; raw < 65536; raw += 7)
        for (int step : {-4096, -1, 0, 1, 4096}) {
            require(GuidanceMath<F>::sineStep(legacy::angleFromWord(raw), step, g_angleLut) ==
                    static_cast<int>((static_cast<int64_t>(sine(static_cast<int16_t>(word(raw))) * step) >> 15)),
                "fixed fine sine step differs from the word formula");
            require(GuidanceMath<F>::cosineStep(legacy::angleFromWord(raw), step, g_angleLut) ==
                    static_cast<int>((static_cast<int64_t>(cosine(static_cast<int16_t>(word(raw))) * step) >> 15)),
                "fixed fine cosine step differs from the word formula");
        }
    /* FineCoord: the 21-bit object ring, map word derivation, and lerp. */
    for (int v : {-1, -0x10, 0, 1, 0x1FFFFF, 0x200000, 0x200000 + 33, 0x7FFFFFFF}) {
        const auto fine = FineCoord<F>::fromRep(v);
        require(MapBoundary<F>::fineWord(fine) == (v & 0x1FFFFF), "fixed fine wrap changed");
        require(fine.mapWord() == static_cast<uint16_t>((v & 0x1FFFFF) >> 5), "fixed fine map word changed");
    }
    for (int a : {0, 100, 0x1FFFFF})
        for (int b : {0, 0x1FFFFF - 100, 0x1FFFFF})
            for (int num : {0, 7, 15})
                require(MapBoundary<F>::fineWord(FineCoord<F>::interpolate(
                            FineCoord<F>::fromRep(a), FineCoord<F>::fromRep(b),
                            FrameFraction::fromTicks(num, 15))) ==
                        a + static_cast<int>((static_cast<int64_t>(b - a) * num) / 15),
                    "fixed fine interpolation differs from lerpLinear");
    /* Plain signed-word difference magnitude, not the wrapped shortest arc. */
    for (int raw = 0; raw < 65536; raw += 13)
        for (int other : {-32768, -4096, 0, 4096, 32767})
            require(legacy::angleSeparation(legacy::angleFromWord(raw), legacy::angleFromWord(other)) ==
                    std::abs(word(raw) - other), "fixed angle separation changed the plain word difference");
}
void projectileModernCases() {
    constexpr double unit = 6.28318530717958647692 / 65536;
    constexpr double pi = 3.14159265358979323846;
    /* atan2 with the compass convention dx=heading-offset, dy=north. */
    require(std::abs(Boundary<M>::radians(GuidanceMath<M>::aimBearing(1, 2)) - std::atan2(1.0, 2.0)) < 1e-14,
        "modern aim bearing quantized or flipped the atan2 convention");
    require(std::abs(Boundary<M>::radians(GuidanceMath<M>::aimBearing(0, 0)) - (-pi)) < 1e-14,
        "modern zero-delta bearing lost the south quirk");
    /* Fractional deltas stay fractional — the bearing must not be computed on
     * word-rounded coordinates (computeTargetBearing regression coverage). */
    require(std::abs(Boundary<M>::radians(GuidanceMath<M>::aimBearing(1.5, 2.25)) -
                     std::atan2(1.5, 2.25)) < 1e-14,
        "modern aim bearing truncated fractional deltas to ints");
    /* Fractional deltas survive steering and clamping. */
    const auto steered = GuidanceMath<M>::turnStep(Boundary<M>::radians(0.25), 15);
    require(std::abs(Boundary<M>::radians(steered) - 1.0 / 15) < 1e-14,
        "modern turn step truncated the fractional delta");
    const auto clamped = GuidanceMath<M>::limitTurn(Boundary<M>::radians(100.5 * unit), -256, 256);
    require(std::abs(Boundary<M>::radians(clamped) - 100.5 * unit) < 1e-14,
        "modern turn clamp truncated the fractional delta");
    const auto wrappedClamp = GuidanceMath<M>::limitTurn(Boundary<M>::radians(-0.51 * pi), -256, 256);
    require(std::abs(Boundary<M>::radians(wrappedClamp) - 256 * unit) < 1e-14,
        "modern turn clamp lost the <= -0x4000 wrap-to-max quirk");
    const auto bank = GuidanceMath<M>::bankFromTurn(Boundary<M>::radians(0.125));
    require(std::abs(Boundary<M>::radians(bank) - 0.25) < 1e-14, "modern bank step truncated");
    require(std::abs(Boundary<M>::radians(GuidanceMath<M>::scaledStep(4096, 15)) - 4096.0 / 15 * unit) < 1e-14,
        "modern scaled step truncated");
    /* Fractional heading survives the fine displacement; no LUT quantization. */
    const auto rad = Boundary<M>::radians(0.6);
    require(std::abs(GuidanceMath<M>::sineStep(rad, 100, g_angleLut) - std::sin(0.6) * 100) < 1e-9,
        "modern fine sine step lost precision");
    require(std::abs(GuidanceMath<M>::cosineStep(rad, 100, g_angleLut) - std::cos(0.6) * 100) < 1e-9,
        "modern fine cosine step lost precision");
    /* FineCoord keeps the sub-fine-unit fraction through wrap and arithmetic. */
    const auto wrapped = FineCoord<M>::fromRep(2097151.75 + 0.5);
    require(std::abs(MapBoundary<M>::fineRep(wrapped) - 0.25) < 1e-12,
        "modern fine wrap discarded the fraction");
    const auto negative = FineCoord<M>::fromRep(-0.25);
    require(std::abs(MapBoundary<M>::fineRep(negative) - 2097151.75) < 1e-12,
        "modern fine negative wrap differs from the ring");
    require(FineCoord<M>::fromRep(65535.9 * 32).mapWord() == 65535,
        "modern fine map word derivation changed");
    const auto lerped = FineCoord<M>::interpolate(FineCoord<M>::fromRep(100.25),
        FineCoord<M>::fromRep(200.75), FrameFraction::fromTicks(1, 2));
    require(std::abs(MapBoundary<M>::fineRep(lerped) - 150.5) < 1e-12,
        "modern fine interpolation truncated");
    /* angleSeparation stays the plain difference: no shortest-arc wrap. */
    const auto sep = legacy::angleSeparation(Boundary<M>::radians(0.9 * pi),
        Boundary<M>::radians(-0.9 * pi));
    require(std::abs(sep - 1.8 * pi / unit) < 1e-9,
        "modern angle separation wrapped onto the shortest arc");
}

/* Bullet tracks: the gun-round state migrated to FineCoord positions with
 * typed fine velocities (the sinMul/cosMul spawn decomposition and the
 * Q12 render/swept-path steps). */
void bulletFixedCases() {
    using rotation_reference::word;
    /* velX/velY/velZ seeding: sinMul/cosMul = rounded Q15 product narrowed to
     * int16 at the destination store. */
    for (int raw = 0; raw < 65536; raw += 11)
        for (int mag : {-32768, -9984, -1, 0, 1, 9984, 32767}) {
            require(GuidanceMath<F>::sineVelocity(legacy::angleFromWord(raw), mag, g_angleLut) ==
                    sinMul(static_cast<int16_t>(word(raw)), static_cast<int16_t>(mag)),
                "fixed bullet sine velocity differs from sinMul");
            require(GuidanceMath<F>::cosineVelocity(legacy::angleFromWord(raw), mag, g_angleLut) ==
                    cosMul(static_cast<int16_t>(word(raw)), static_cast<int16_t>(mag)),
                "fixed bullet cosine velocity differs from cosMul");
        }
    /* Signed ring delta: (a - b) & mask, centered past the half ring. */
    for (int a : {0, 1, 0x100000, 0x1FFFFF})
        for (int b : {-1, 0, 33, 0x100000, 0x1FFFFF, 0x200000}) {
            long d = (static_cast<long>(a & 0x1FFFFF) - b) & 0x1FFFFF;
            if (d & 0x100000) d -= 0x200000;
            require(FineCoord<F>::fromRep(a).deltaFrom(b) == d,
                "fixed fine ring delta changed");
        }
    /* Free-slot convention: rep 0, reached through default and writes. */
    require(FineCoord<F>::fromRep(0).isZero() && !FineCoord<F>::fromRep(1).isZero(),
        "fixed fine zero test changed");
    /* Render/trajectory step: (vel * alphaQ12) >> 12, int32 product first. */
    for (int vel : {-9984, -1, 0, 1, 9984, 300000})
        for (int alpha : {0, 1, 2048, 4096})
            require(GuidanceMath<F>::fineTravel(vel, alpha) ==
                    static_cast<int32_t>(vel * alpha) >> 12,
                "fixed Q12 travel step changed");
}
void bulletModernCases() {
    const auto rad = Boundary<M>::radians(0.6);
    /* Velocity components keep the exact trig product — no LUT, no round, no
     * int16 narrow of the magnitude. */
    require(std::abs(GuidanceMath<M>::sineVelocity(rad, 9984, g_angleLut) - std::sin(0.6) * 9984) < 1e-9,
        "modern bullet sine velocity truncated");
    require(std::abs(GuidanceMath<M>::cosineVelocity(rad, 9984, g_angleLut) - std::cos(0.6) * 9984) < 1e-9,
        "modern bullet cosine velocity truncated");
    /* Fractional positions survive accumulation and the ring delta stays
     * centered without quantizing either endpoint. */
    const auto pos = FineCoord<M>::fromRep(2097151.75);
    const auto advanced = pos.advanced(GuidanceMath<M>::fineTravel(10.5, 2048));
    require(std::abs(MapBoundary<M>::fineRep(advanced) - (2097151.75 + 5.25 - 2097152.0)) < 1e-12,
        "modern fine advance lost the fractional step across the wrap");
    require(std::abs(pos.deltaFrom(0.25) - (-0.5)) < 1e-12,
        "modern fine ring delta did not center across the seam");
    require(std::abs(FineCoord<M>::fromRep(100.5).deltaFrom(99.25) - 1.25) < 1e-12,
        "modern fine ring delta truncated");
    require(std::abs(GuidanceMath<M>::fineTravel(10.5, 2048) - 5.25) < 1e-12,
        "modern Q12 travel step truncated");
}
}
int main() {
    fixedCases(); modernCases(); recoveryCases(); modernApproachCases();
    projectileFixedCases(); projectileModernCases();
    bulletFixedCases(); bulletModernCases();
}
