#ifndef F15_MATH_INTERPOLATION_HPP
#define F15_MATH_INTERPOLATION_HPP

#include "rotation.hpp"
#include <limits>
#include <stdexcept>

namespace f15::math {

class FrameFraction {
    std::int64_t numerator_, denominator_;
    FrameFraction(std::int64_t n, std::int64_t d) : numerator_(n), denominator_(d) {}
    template<class B> friend class PoseInterpolation;
    template<class B> friend class HorizontalMath;
    template<class B> friend class AltitudeMath;
    template<class B> friend class MapMath;
    template<class B> friend class FineCoord;
public:
    // Scheduler counters are integral; reject extrapolation and fixed-product overflow.
    static FrameFraction fromTicks(std::int64_t elapsed, std::int64_t duration) {
        if (duration <= 0 || elapsed < 0 || elapsed > duration ||
            elapsed > std::numeric_limits<std::int64_t>::max() / 32768)
            throw std::domain_error("invalid frame interpolation interval");
        return FrameFraction(elapsed, duration);
    }
};

template<class B> class PoseInterpolation {
    static auto delta(Angle<B> a, Angle<B> b) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto bits = static_cast<std::uint16_t>(b.value_.raw() - a.value_.raw());
            return bits <= 32767 ? static_cast<int>(bits) : static_cast<int>(bits) - 65536;
        } else {
            return std::remainder(b.value_ - a.value_, 6.28318530717958647692);
        }
    }
    static Angle<B> blend(Angle<B> a, Angle<B> b, FrameFraction fraction) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto step = delta(a, b) * fraction.numerator_ / fraction.denominator_;
            return Angle<B>(fixed::Angle16(a.value_.raw() + static_cast<int>(step)));
        } else {
            return Angle<B>(a.value_ + delta(a, b) *
                           (static_cast<double>(fraction.numerator_) / fraction.denominator_));
        }
    }
public:
    // Trim is a signed angular offset: interpolate linearly, not across the seam.
    static Angle<B> linearOffset(Angle<B> a, Angle<B> b, FrameFraction fraction) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto start = a.value_.signedRaw(), end = b.value_.signedRaw();
            const auto difference = std::int64_t(end) - start;
            const auto magnitude = difference < 0 ? -difference : difference;
            if (magnitude && fraction.numerator_ > INT64_MAX / magnitude)
                throw std::overflow_error("trim interpolation product overflow");
            return Angle<B>(fixed::Angle16(start + static_cast<int>(difference * fraction.numerator_ / fraction.denominator_)));
        } else {
            const double t = double(fraction.numerator_) / fraction.denominator_;
            return Angle<B>(a.value_ * (1 - t) + b.value_ * t);
        }
    }
    static bool snaps(Angle<B> a, Angle<B> b) {
        const auto d = delta(a, b);
        if constexpr (std::is_same_v<B, FixedBackend>) return d >= 16384 || d <= -16384;
        else return std::abs(d) >= 1.57079632679489661923;
    }
    /* Single-angle shortest-arc lerp — the standalone lerpAngle: snap across
     * the seam, otherwise blend by the frame fraction. */
    static Angle<B> angle(Angle<B> a, Angle<B> b, FrameFraction fraction) {
        return snaps(a, b) ? b : blend(a, b, fraction);
    }
    static EulerAngles<B> interpolate(EulerAngles<B> a, EulerAngles<B> b, FrameFraction fraction) {
        // Euler flips describe a single discontinuity: snap the entire pose together.
        if (snaps(a.yaw, b.yaw) || snaps(a.pitch, b.pitch) || snaps(a.roll, b.roll)) return b;
        return {blend(a.yaw, b.yaw, fraction), blend(a.pitch, b.pitch, fraction),
                blend(a.roll, b.roll, fraction)};
    }
};
} // namespace f15::math
#endif
