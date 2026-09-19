#ifndef F15_MATH_LEGACY_ROTATION_HPP
#define F15_MATH_LEGACY_ROTATION_HPP

// Temporary adapter for unmigrated globals. Do not use in typed domain code.
#define F15_MATH_BOUNDARY_ACCESS
#include "boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS

namespace f15::math::legacy {
using Codec = Boundary<FixedBackend>;
using Math = RotationMath<FixedBackend>;
using AircraftAngle = Angle<FixedBackend>;

// Read-only adapters for consumers that have not migrated their scalar math yet.
inline std::int16_t signedAngle(AircraftAngle angle) {
    const auto bits = Codec::angleWord(angle);
    return static_cast<std::int16_t>(bits <= 32767 ? bits : static_cast<int>(bits) - 65536);
}
inline AircraftAngle angleFromWord(int word) {
    return Codec::angleWord(static_cast<std::uint16_t>(word));
}

inline int updateAttitudeFromWords(AircraftAngle &roll, AircraftAngle &pitch,
                                  int (*update)(std::int16_t *, std::int16_t *)) {
    auto rollWord = signedAngle(roll), pitchWord = signedAngle(pitch);
    const int result = update(&rollWord, &pitchWord);
    roll = angleFromWord(rollWord);
    pitch = angleFromWord(pitchWord);
    return result;
}

inline EulerAngles<FixedBackend> angles(int yaw, int pitch, int roll) {
    return {Codec::angleWord(static_cast<std::uint16_t>(yaw)),
            Codec::angleWord(static_cast<std::uint16_t>(pitch)),
            Codec::angleWord(static_cast<std::uint16_t>(roll))};
}

inline void storeTerms(const RotationTerms<FixedBackend> &terms,
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
