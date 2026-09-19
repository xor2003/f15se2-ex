#ifndef F15_MATH_FLIGHT_CONTROL_HPP
#define F15_MATH_FLIGHT_CONTROL_HPP

#include "rotation.hpp"
#include <stdexcept>

namespace f15::math {
struct RollAxis {};
struct PitchAxis {};
struct YawAxis {};
template<class B> struct ControlBoundary;

// Fixed commands retain the original storage widths; modern rates are radians/second.
// One legacy roll/pitch unit requests 128 angle words/second, one yaw unit one word/second.
template<class B, class Axis> class AxisRate {
    static_assert(std::is_same_v<Axis, RollAxis> || std::is_same_v<Axis, PitchAxis> ||
                  std::is_same_v<Axis, YawAxis>);
    using FixedRep = std::conditional_t<std::is_same_v<Axis, RollAxis>, std::int32_t, std::int16_t>;
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, FixedRep, double>;
    Rep value_{};
    explicit AxisRate(Rep value) : value_(value) {}
    friend class FlightControlMath<B>;
    friend struct ControlBoundary<B>;
    static AxisRate sum(AxisRate a, AxisRate b) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            using Unsigned = std::make_unsigned_t<FixedRep>;
            const auto bits = static_cast<Unsigned>(static_cast<Unsigned>(a.value_) + static_cast<Unsigned>(b.value_));
            constexpr std::int64_t modulus = std::is_same_v<Axis, RollAxis> ? 4294967296LL : 65536;
            const auto value = static_cast<std::int64_t>(bits);
            return AxisRate(static_cast<Rep>(value >= modulus / 2 ? value - modulus : value));
        } else {
            const auto value = a.value_ + b.value_;
            if (!std::isfinite(value)) throw std::overflow_error("angular rate overflow");
            return AxisRate(value);
        }
    }
public:
    AxisRate() = default;
    bool isZero() const { return value_ == 0; }
    bool isNegative() const { return value_ < 0; }
    AxisRate operator+(AxisRate other) const { return sum(*this, other); }
    AxisRate &operator+=(AxisRate other) { return *this = *this + other; }
};
template<class B> using RollCommand = AxisRate<B, RollAxis>;
template<class B> using PitchCommand = AxisRate<B, PitchAxis>;
template<class B> using YawRate = AxisRate<B, YawAxis>;

template<class B> class SimulationStep {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, int, double>;
    Rep value_;
    explicit SimulationStep(Rep value) : value_(value) {}
    friend class FlightControlMath<B>;
    friend class AltitudeMath<B>;
    friend class HorizontalMath<B>;
    friend class AirspeedMath<B>;
    friend class AerodynamicsMath<B>;
    friend struct ControlBoundary<B>;
};

class JoystickSample {
    std::uint8_t roll_, pitch_;
    JoystickSample(std::uint8_t roll, std::uint8_t pitch) : roll_(roll), pitch_(pitch) {}
    template<class B> friend class FlightControlMath;
    template<class B> friend struct ControlBoundary;
};

template<class B> struct FlightCommands { RollCommand<B> roll; PitchCommand<B> pitch; };
template<class B> struct RotationDeltas { Angle<B> yaw, pitch, roll; };

template<class B> class FlightControlMath {
    static std::int16_t word(std::int64_t v) {
        const auto bits = static_cast<std::uint16_t>(v);
        return static_cast<std::int16_t>(bits <= 32767 ? bits : static_cast<int>(bits) - 65536);
    }
    static constexpr double wordRadians = 6.28318530717958647692 / 65536;
public:
    static FlightCommands<B> fromJoystick(JoystickSample sample) {
        int r = (sample.roll_ >> 4) - 8, p = (sample.pitch_ >> 4) - 8;
        if (r < 0) ++r;
        if (p < 0) ++p;
        r = -(std::abs(r) + 2) * r * 2;
        p *= 6;
        if (p < 0) p /= 2;
        if constexpr (std::is_same_v<B, FixedBackend>)
            return {RollCommand<B>(r), PitchCommand<B>(static_cast<std::int16_t>(p))};
        else return {RollCommand<B>(r * 128 * wordRadians), PitchCommand<B>(p * 128 * wordRadians)};
    }
    static YawRate<B> groundYaw(RollCommand<B> roll) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return YawRate<B>(word(-static_cast<std::int64_t>(roll.value_) * 64));
        else return YawRate<B>(-roll.value_ / 2);
    }
    static RotationDeltas<B> increments(RollCommand<B> roll, PitchCommand<B> pitch,
                                       YawRate<B> yaw, SimulationStep<B> step) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto bits = static_cast<std::uint32_t>(roll.value_) * std::uint32_t{128};
            const std::int64_t rollProduct = bits <= 0x7fffffff ? bits : static_cast<std::int64_t>(bits) - 4294967296LL;
            const auto pitchProduct = word(static_cast<std::int64_t>(pitch.value_) * 128);
            return {Angle<B>(fixed::Angle16(yaw.value_ / step.value_)),
                    Angle<B>(fixed::Angle16(pitchProduct / step.value_)),
                    Angle<B>(fixed::Angle16(static_cast<int>(rollProduct / step.value_)))};
        } else {
            return {Angle<B>(yaw.value_ * step.value_), Angle<B>(pitch.value_ * step.value_),
                    Angle<B>(roll.value_ * step.value_)};
        }
    }
};
} // namespace f15::math
#endif
