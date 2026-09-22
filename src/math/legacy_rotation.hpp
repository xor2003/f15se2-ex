#ifndef F15_MATH_LEGACY_ROTATION_HPP
#define F15_MATH_LEGACY_ROTATION_HPP

// Temporary adapter for unmigrated globals. Do not use in typed domain code.
#define F15_MATH_BOUNDARY_ACCESS
#include "boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
#include "../inttype.h"
#include <cmath>
#include <type_traits>

namespace f15::math::legacy {
using Codec = Boundary<GameBackend>;
using Math = RotationMath<GameBackend>;
using AircraftAngle = Angle<GameBackend>;

// Read-only adapters for consumers that have not migrated their scalar math yet.
inline std::int16_t signedAngle(AircraftAngle angle) {
    const auto bits = Codec::angleWord(angle);
    return static_cast<std::int16_t>(bits <= 32767 ? bits : static_cast<int>(bits) - 65536);
}
inline AircraftAngle angleFromWord(int word) {
    return Codec::angleWord(static_cast<std::uint16_t>(word));
}

/* Angle magnitude in word units (0..32768) for magnitude-threshold decisions.
 * Fixed reproduces the hosted abs(int16) result; modern keeps the sub-word
 * fraction that signedAngle() would round away. Templated on the backend so
 * the fixed-only member lookups stay out of the modern instantiation. */
template<class B = GameBackend>
inline auto angleMagnitude(Angle<B> angle) {
    if constexpr (std::is_same_v<B, FixedBackend>)
        return std::abs(static_cast<int>(signedAngle(angle)));
    else
        return std::fabs(Boundary<B>::radians(angle)) * (32768.0 / 3.14159265358979323846);
}

/* Same read for the abs16Compat quirk sites — and for post-abs (int16) casts,
 * which re-wrap 32768 to -32768 identically. The DOS abs() left word 0x8000
 * negative; fixed preserves that, modern returns the clean magnitude. */
template<class B = GameBackend>
inline auto angleMagnitudeCompat(Angle<B> angle) {
    if constexpr (std::is_same_v<B, FixedBackend>)
        return abs16Compat(signedAngle(angle));
    else
        return angleMagnitude(angle);
}

/* Plain signed-word difference magnitude |a - b| in word units — the original
 * subtracted raw int16s without wrapping onto the shortest arc. The wrapped
 * form is angleMagnitude(a - b); this adapter preserves the unwrapped read. */
template<class B = GameBackend>
inline auto angleSeparation(Angle<B> a, Angle<B> b) {
    if constexpr (std::is_same_v<B, FixedBackend>) {
        const auto wordOf = [](Angle<B> v) {
            const auto bits = Boundary<B>::angleWord(v);
            return bits <= 32767 ? static_cast<int>(bits) : static_cast<int>(bits) - 65536;
        };
        return std::abs(wordOf(a) - wordOf(b));
    } else {
        return std::fabs(Boundary<B>::radians(a) - Boundary<B>::radians(b))
               * (32768.0 / 3.14159265358979323846);
    }
}

inline int updateAttitudeFromWords(AircraftAngle &roll, AircraftAngle &pitch,
                                  int (*update)(std::int16_t *, std::int16_t *)) {
    auto rollWord = signedAngle(roll), pitchWord = signedAngle(pitch);
    const auto originalRoll = rollWord;
    const auto originalPitch = pitchWord;
    const int result = update(&rollWord, &pitchWord);
    if (rollWord != originalRoll) roll = angleFromWord(rollWord);
    if (pitchWord != originalPitch) pitch = angleFromWord(pitchWord);
    return result;
}

/* Signed word-domain rep: fixed returns the int16 word, modern returns
 * fractional word units. An unwrapped view for control-signal diffs like
 * `pitchCmd - pitch` that the originals computed as raw int subtracts —
 * arc-wrapping them through Angle::operator- would flip the clamp side. */
template<class B = GameBackend>
inline auto wordRep(Angle<B> angle) {
    if constexpr (std::is_same_v<B, FixedBackend>)
        return static_cast<std::int16_t>(Boundary<B>::angleWord(angle));
    else
        return Boundary<B>::radians(angle) * (65536.0 / 6.28318530717958647692);
}

/* Unsigned word-domain rep — the (uint16) cast domain: fixed wraps to
 * 0..65535, modern to [0, 65536) keeping the fraction. */
template<class B = GameBackend>
inline auto uwordRep(Angle<B> angle) {
    if constexpr (std::is_same_v<B, FixedBackend>)
        return Boundary<B>::angleWord(angle);
    else {
        const double rep = Boundary<B>::radians(angle) * (65536.0 / 6.28318530717958647692);
        return rep < 0 ? rep + 65536.0 : rep;
    }
}

/* clampRange(value, lo, hi) on word-domain scalars — including the legacy
 * `value <= -0x4000 selects hi` wrapped-angle quirk. Works on int (fixed
 * semantics) or double (modern keeps the in-range fraction). */
template<class T>
inline T wordClamp(T value, int minVal, int maxVal) {
    if (value > static_cast<T>(maxVal)) return static_cast<T>(maxVal);
    if (value >= static_cast<T>(minVal)) return value;
    if (value <= static_cast<T>(-0x4000)) return static_cast<T>(maxVal);
    return static_cast<T>(minVal);
}

/* (words / divisor) as an angle step — the `angleWord += x / scaling`
 * integration the AI spelled inline. Fixed keeps the truncating int divide;
 * modern keeps the quotient fractional so sub-word steps accumulate. */
template<class B = GameBackend, class T>
inline Angle<B> attitudeStep(T words, int divisor) {
    if constexpr (std::is_same_v<B, FixedBackend>)
        return Boundary<B>::angleWord(static_cast<std::uint16_t>(
            static_cast<int>(words / divisor)));
    else
        return Boundary<B>::radians(static_cast<double>(words) / divisor *
                                    (6.28318530717958647692 / 65536));
}

/* ((uint16 rep) * speed) >> 14 — the Q14 word product the moveAmt formula
 * spelled with raw casts. Fixed keeps the uint32 wrap+shift; modern floors
 * the honest product before the int16 boundary. */
template<class B = GameBackend>
inline std::int16_t wordProductQ14(std::uint16_t uword, std::int16_t speed) {
    return static_cast<std::int16_t>(static_cast<std::uint32_t>(uword) *
                                     static_cast<std::int32_t>(speed) >> 14);
}
template<class B = GameBackend>
inline std::int16_t wordProductQ14(double uword, std::int16_t speed) {
    return static_cast<std::int16_t>(static_cast<std::int32_t>(
        std::floor(uword * speed / 16384.0)));
}

/* Word-domain scalar rep matching wordRep: int16 under fixed, double under
 * modern — for control-signal quantities (command deltas) that stay raw. */
template<class B = GameBackend>
using WordScalar = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;

/* Typed attitude shadow <-> packed int16 sync for SimObject heading/pitch/
 * bank — the same shadow pattern as the fine positions. The shadow is
 * authoritative; the packed word mirrors it for layout/render/serialization
 * consumers (fixed mirrors exactly; modern rounds the fraction to the word). */
template<class B = GameBackend>
inline void objectAttitudeSet(Angle<B> &shadow, std::int16_t &packed, Angle<B> value) {
    shadow = value;
    packed = static_cast<std::int16_t>(Boundary<B>::angleWord(value));
}
template<class B = GameBackend>
inline void objectAttitudeAdvance(Angle<B> &shadow, std::int16_t &packed, Angle<B> step) {
    objectAttitudeSet<B>(shadow, packed, shadow + step);
}

inline EulerAngles<GameBackend> angles(int yaw, int pitch, int roll) {
    return {Codec::angleWord(static_cast<std::uint16_t>(yaw)),
            Codec::angleWord(static_cast<std::uint16_t>(pitch)),
            Codec::angleWord(static_cast<std::uint16_t>(roll))};
}

inline void storeTerms(const RotationTerms<GameBackend> &terms,
                       std::int16_t &sinYaw, std::int16_t &cosYaw,
                       std::int16_t &sinPitch, std::int16_t &cosPitch,
                       std::int16_t &sinRoll, std::int16_t &cosRoll) {
    sinYaw = Codec::coefficientWord(terms.sinYaw);
    cosYaw = Codec::coefficientWord(terms.cosYaw);
    sinPitch = Codec::coefficientWord(terms.sinPitch);
    cosPitch = Codec::coefficientWord(terms.cosPitch);
    sinRoll = Codec::coefficientWord(terms.sinRoll);
    cosRoll = Codec::coefficientWord(terms.cosRoll);
}
} // namespace f15::math::legacy
#endif
