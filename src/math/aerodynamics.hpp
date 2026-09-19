#ifndef F15_MATH_AERODYNAMICS_HPP
#define F15_MATH_AERODYNAMICS_HPP
#include "airspeed.hpp"

namespace f15::math {
// The legacy "lift force" is an angular correction, not a force in newtons.
template<class B> class AerodynamicsMath {
    static int signedWord(std::int64_t v) {
        const auto bits = std::uint16_t(v);
        return bits < 32768 ? int(bits) : int(bits) - 65536;
    }
public:
    static Angle<B> liftCorrection(FlightSpeed<B> stallSpeed, FlightSpeed<B> speed) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            // abs(INT_MIN) and abs(INT_MAX)+1 were undefined in the old formula.
            if (speed.value_ <= -INT32_MAX || speed.value_ == INT32_MAX)
                throw std::overflow_error("lift denominator outside native signed range");
            const auto magnitude = speed.value_ < 0 ? -speed.value_ : speed.value_;
            auto result = signedWord(signedWord(stallSpeed.value_) * 3072 / (magnitude + 1));
            if (std::uint16_t(result) > 8192) result = 8192;
            return Angle<B>(fixed::Angle16(result));
        } else {
            const auto correction = stallSpeed.value_ / (std::abs(speed.value_) + 1) * 3072;
            if (!std::isfinite(correction)) throw std::overflow_error("non-finite lift correction");
            return Angle<B>(std::clamp(correction, 0.0, 8192.0) * (6.28318530717958647692 / 65536));
        }
    }
    static Angle<B> pitchTrim(Angle<B> lift, Coefficient<B> cosineRoll) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto correction = signedWord(lift.value_.raw() - 768);
            const auto product = std::int64_t(correction) * cosineRoll.value_ + 16384;
            const auto rounded = product / 32768 - (product % 32768 < 0 ? 1 : 0);
            return Angle<B>(fixed::Angle16(signedWord(rounded)));
        } else {
            const auto result = (lift.value_ - 768 * (6.28318530717958647692 / 65536)) * cosineRoll.value_;
            if (!std::isfinite(result)) throw std::overflow_error("non-finite pitch trim");
            return Angle<B>(result);
        }
    }
};
}
#endif
