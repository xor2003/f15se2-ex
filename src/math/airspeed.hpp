#ifndef F15_MATH_AIRSPEED_HPP
#define F15_MATH_AIRSPEED_HPP
#include "altitude.hpp"
#include "horizontal.hpp"
#include <algorithm>

namespace f15::math {
template<class B> struct AirspeedBoundary;
template<class B> class AirspeedMath;
struct FlightSpeedUnit {};
struct DecelerationUnit {};
template<class B, class Unit> class AirspeedQuantity {
    static_assert(std::is_same_v<B, FixedBackend> || std::is_same_v<B, ModernBackend>);
    static_assert(std::is_same_v<Unit, FlightSpeedUnit> || std::is_same_v<Unit, DecelerationUnit>);
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int32_t, double>;
    Rep value_{};
    explicit AirspeedQuantity(Rep v) : value_(v) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(v)) throw std::overflow_error("non-finite airspeed quantity");
    }
    friend struct AirspeedBoundary<B>;
    friend class AirspeedMath<B>;
    friend class AerodynamicsMath<B>;
public:
    AirspeedQuantity() = default;
    bool operator==(AirspeedQuantity other) const { return value_ == other.value_; }
    bool isZero() const { return value_ == 0; }
};
template<class B> using FlightSpeed = AirspeedQuantity<B, FlightSpeedUnit>;
template<class B> using Deceleration = AirspeedQuantity<B, DecelerationUnit>;

// Engine velocity units (27 per indicated knot), not SI metres per second.
template<class B> class AirspeedMath {
public:
    static FlightSpeed<B> accelerate(FlightSpeed<B> speed, FlightSpeed<B> target, SimulationStep<B> step) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto difference = std::int64_t(target.value_) - speed.value_;
            // Reject the old signed-overflow domain instead of inventing parity.
            if (difference < INT32_MIN || difference > INT32_MAX)
                throw std::overflow_error("airspeed acceleration difference overflow");
            return FlightSpeed<B>(static_cast<std::int32_t>(speed.value_ + difference / 16 / step.value_));
        } else return FlightSpeed<B>(speed.value_ + (target.value_ - speed.value_) / 16 * step.value_);
    }
    static FlightSpeed<B> groundBrake(FlightSpeed<B> speed, Deceleration<B> deceleration,
                                       SimulationStep<B> step) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto result = std::int64_t(speed.value_) - deceleration.value_ / step.value_;
            if (result < INT32_MIN || result > INT32_MAX)
                throw std::overflow_error("airspeed braking overflow");
            return FlightSpeed<B>(static_cast<std::int32_t>(result));
        } else return FlightSpeed<B>(speed.value_ - deceleration.value_ * step.value_);
    }
    static FlightSpeed<B> airBrake(FlightSpeed<B> speed, SimulationStep<B> step) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto decrement = (std::uint16_t(speed.value_) >> 4) / step.value_;
            return groundBrake(speed, Deceleration<B>(decrement), SimulationStep<B>(1));
        } else return FlightSpeed<B>(speed.value_ - speed.value_ / 16 * step.value_);
    }
    static FlightSpeed<B> carrierStop(FlightSpeed<B> speed) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return std::uint16_t(speed.value_) < 432 ? FlightSpeed<B>{} : speed;
        else return speed.value_ < 432 ? FlightSpeed<B>{} : speed;
    }
    static FlightSpeed<B> constrain(FlightSpeed<B> speed) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return std::uint16_t(speed.value_) > 45000 ? FlightSpeed<B>{} : speed;
        else return FlightSpeed<B>(std::max(0.0, std::min(45000.0, speed.value_)));
    }
    static AirspeedSample<B> verticalSample(FlightSpeed<B> speed) {
        if constexpr (std::is_same_v<B, FixedBackend>) return AirspeedSample<B>(std::uint16_t(speed.value_));
        else {
            if (speed.value_ < 0) throw std::domain_error("negative vertical airspeed sample");
            return AirspeedSample<B>(speed.value_);
        }
    }
    static HorizontalSpeed<B> horizontalSample(FlightSpeed<B> speed, Coefficient<B> cosinePitch) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            // cosMul's second argument is a signed word, despite native-width storage.
            const auto bits = std::uint16_t(speed.value_);
            const auto signedSpeed = bits < 32768 ? int(bits) : int(bits) - 65536;
            const auto product = std::int64_t(signedSpeed) * cosinePitch.value_ + 16384;
            const auto result = product / 32768 - (product % 32768 < 0 ? 1 : 0);
            const auto word = std::uint16_t(result);
            return HorizontalSpeed<B>(static_cast<std::int16_t>(word < 32768 ? int(word) : int(word) - 65536));
        } else return HorizontalSpeed<B>(speed.value_ * cosinePitch.value_);
    }
};
}
#endif
