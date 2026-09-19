#ifndef F15_MATH_GUIDANCE_HPP
#define F15_MATH_GUIDANCE_HPP
#include "altitude.hpp"
#include <algorithm>

namespace f15::math {
template<class B> class GuidanceMath {
    static constexpr int headingLimit = 5120;
    static constexpr int climbLimit = 3072;
    static constexpr int heightGain = 16;
    static constexpr int rollDivisor = 64;
    static constexpr int pitchDivisor = 128;
    static constexpr int rollLimit = 24;
    static constexpr int pitchLimit = 8;
    static constexpr double wordRadians = 6.28318530717958647692 / 65536;
    static int word(int value) {
        const auto bits = std::uint16_t(value);
        return bits < 32768 ? int(bits) : int(bits) - 65536;
    }
    static int floorDivide(int value, int divisor) {
        return value / divisor - (value % divisor < 0 ? 1 : 0);
    }
public:
    // Targets use the scene-height scale stored by the original autopilot,
    // not expanded flight altitude. Recovery-waypoint steering is separate.
    static FlightCommands<B> altitudeHold(RenderHeight<B> target, RenderHeight<B> height,
        EulerAngles<B> attitude, Angle<B> bearing, Angle<B> headingOffset, Angle<B> trim) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const int headingError = word(int(headingOffset.value_.raw()) -
                int(attitude.yaw.value_.raw()) + int(bearing.value_.raw()));
            const int bankTarget = std::clamp(headingError, -headingLimit, headingLimit) * 2;
            const int roll = -std::clamp(floorDivide(word(bankTarget - int(attitude.roll.value_.raw())),
                rollDivisor), -rollLimit, rollLimit);
            const int pitchTarget = std::clamp((int(target.value_) - height.value_) * heightGain -
                word(trim.value_.raw()), -headingLimit, climbLimit);
            const int pitch = std::clamp(floorDivide(pitchTarget - word(attitude.pitch.value_.raw()),
                pitchDivisor), -pitchLimit, pitchLimit);
            return {RollCommand<B>(roll), PitchCommand<B>(static_cast<std::int16_t>(pitch))};
        } else {
            const double headingError = (headingOffset - attitude.yaw + bearing).value_;
            const double bankTarget = std::clamp(headingError, -headingLimit * wordRadians,
                headingLimit * wordRadians) * 2;
            const double rollError = (Angle<B>(bankTarget) - attitude.roll).value_;
            const double altitudeError = (target.value_ - height.value_) * (heightGain * wordRadians) - trim.value_;
            if (!std::isfinite(rollError) || !std::isfinite(altitudeError) || !std::isfinite(attitude.pitch.value_))
                throw std::domain_error("non-finite altitude-hold input");
            const double pitchTarget = std::clamp(altitudeError, -headingLimit * wordRadians, climbLimit * wordRadians);
            const double roll = -std::clamp(rollError / (rollDivisor * wordRadians), -double(rollLimit), double(rollLimit));
            const double pitch = std::clamp((pitchTarget - attitude.pitch.value_) / (pitchDivisor * wordRadians),
                -double(pitchLimit), double(pitchLimit));
            return {RollCommand<B>(roll * pitchDivisor * wordRadians), PitchCommand<B>(pitch * pitchDivisor * wordRadians)};
        }
    }
};
}
#endif
