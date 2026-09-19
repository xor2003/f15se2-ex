#ifndef F15_MATH_AERODYNAMICS_HPP
#define F15_MATH_AERODYNAMICS_HPP
#include "airspeed.hpp"

namespace f15::math {
enum class StallSeverity { Normal, Severe };
template<class B> struct StallResponse { bool stalled; Angle<B> noseDrop; };
// The legacy "lift force" is an angular correction, not a force in newtons.
template<class B> class AerodynamicsMath {
    static int signedWord(std::int64_t v) {
        const auto bits = std::uint16_t(v);
        return bits < 32768 ? int(bits) : int(bits) - 65536;
    }
public:
    static bool aboveStall(FlightSpeed<B> speed, StallSpeed<B> threshold) {
        if constexpr (std::is_same_v<B, FixedBackend>) return std::uint16_t(speed.value_) > std::uint16_t(threshold.value_);
        else return speed.value_ > threshold.value_;
    }
    static bool belowStall(FlightSpeed<B> speed, StallSpeed<B> threshold) {
        if constexpr (std::is_same_v<B, FixedBackend>) return std::uint16_t(speed.value_) < std::uint16_t(threshold.value_);
        else return speed.value_ < threshold.value_;
    }
    static StallResponse<B> stallResponse(FlightSpeed<B> speed, StallSpeed<B> threshold,
                                          StallSeverity severity, SimulationStep<B> step) {
        if (severity != StallSeverity::Normal && severity != StallSeverity::Severe)
            throw std::domain_error("invalid stall severity");
        if (!belowStall(speed, threshold)) return {false, {}};
        const int divisor = severity == StallSeverity::Severe ? 2 : 4;
        if constexpr (std::is_same_v<B, FixedBackend>) {
            // Legacy stall correction is per tick, without frequency scaling.
            const int deficit = int(std::uint16_t(threshold.value_)) - int(std::uint16_t(speed.value_));
            return {true, Angle<B>(fixed::Angle16(deficit / divisor))};
        } else {
            const double deficit = threshold.value_ - speed.value_;
            if (!std::isfinite(deficit)) throw std::overflow_error("stall speed deficit overflow");
            // Calibrate to the current 15 Hz simulation, retaining fractions at other rates.
            const double radians = deficit * ((15 * step.value_ / divisor) * (6.28318530717958647692 / 65536));
            return {true, Angle<B>(radians)};
        }
    }
    static Angle<B> liftCorrection(StallSpeed<B> stallSpeed, FlightSpeed<B> speed) {
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
