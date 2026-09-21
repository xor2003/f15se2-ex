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
