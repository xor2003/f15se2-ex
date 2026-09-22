#ifndef F15_MATH_HORIZONTAL_HPP
#define F15_MATH_HORIZONTAL_HPP
#include "flight_control.hpp"
#include "interpolation.hpp"

namespace f15::math {
struct ViewXAxis {};
struct ViewYAxis {};
template<class B> struct HorizontalBoundary;
template<class B> class HorizontalMath;
template<class B, class Axis> class ViewDisplacement;

/* Fine-coordinate rep (int32 fixed / double modern): the sub-fine-unit
 * fraction survives under modern; under fixed it is the original int32 word. */
template<class B> using FineRep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int32_t, double>;

namespace detail {
inline std::int32_t signedDword(std::uint32_t bits) {
    return static_cast<std::int32_t>(bits <= INT32_MAX ? std::int64_t(bits) : std::int64_t(bits) - 4294967296LL);
}
}

// Fine coordinates in the player/view frame, including its inverted map Y.
template<class B, class Axis> class ViewCoordinate {
    static_assert(std::is_same_v<Axis, ViewXAxis> || std::is_same_v<Axis, ViewYAxis>);
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int32_t, double>;
    Rep value_{};
    explicit ViewCoordinate(Rep v) : value_(v) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(v)) throw std::overflow_error("non-finite horizontal coordinate");
    }
    friend struct HorizontalBoundary<B>;
    friend class HorizontalMath<B>;
public:
    ViewCoordinate() = default;
    bool operator==(ViewCoordinate other) const { return value_ == other.value_; }
    bool operator!=(ViewCoordinate other) const { return !(*this == other); }
    ViewCoordinate operator+(ViewDisplacement<B, Axis> delta) const {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return ViewCoordinate(detail::signedDword(std::uint32_t(value_) + std::uint32_t(delta.value_)));
        else return ViewCoordinate(value_ + delta.value_);
    }
    ViewCoordinate operator-(ViewDisplacement<B, Axis> delta) const {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return ViewCoordinate(detail::signedDword(std::uint32_t(value_) - std::uint32_t(delta.value_)));
        else return ViewCoordinate(value_ - delta.value_);
    }
    ViewCoordinate &operator+=(ViewDisplacement<B, Axis> delta) { return *this = *this + delta; }
    ViewCoordinate &operator-=(ViewDisplacement<B, Axis> delta) { return *this = *this - delta; }
};
template<class B, class Axis> class ViewDisplacement {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int32_t, double>;
    Rep value_{};
    explicit ViewDisplacement(Rep v) : value_(v) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(v)) throw std::overflow_error("non-finite horizontal displacement");
    }
    friend class ViewCoordinate<B, Axis>;
    friend class HorizontalMath<B>;
    friend struct HorizontalBoundary<B>;
public:
    ViewDisplacement() = default;
};
template<class B> class HorizontalSpeed {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    Rep value_{};
    explicit HorizontalSpeed(Rep v) : value_(v) {}
    friend class HorizontalMath<B>;
    friend struct HorizontalBoundary<B>;
    friend class AirspeedMath<B>;
public:
    HorizontalSpeed() = default;
};
template<class B> struct HorizontalStep { ViewDisplacement<B, ViewXAxis> x; ViewDisplacement<B, ViewYAxis> y; };
template<class B> class HorizontalMath {
    static auto component(HorizontalSpeed<B> speed, Coefficient<B> direction, SimulationStep<B> step) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto p = std::int64_t(speed.value_) * direction.value_ + 16384;
            const auto rounded = p / 32768 - (p % 32768 < 0 ? 1 : 0);
            return static_cast<std::int32_t>(rounded / 10 / step.value_);
        } else return speed.value_ * direction.value_ / 10 * step.value_;
    }
public:
    static HorizontalStep<B> increments(HorizontalSpeed<B> speed, Coefficient<B> sineHeading,
                                        Coefficient<B> cosineHeading, SimulationStep<B> step) {
        return {ViewDisplacement<B, ViewXAxis>(component(speed, sineHeading, step)),
                ViewDisplacement<B, ViewYAxis>(component(speed, cosineHeading, step))};
    }
    template<class Axis> static ViewCoordinate<B, Axis> approach(ViewCoordinate<B, Axis> from,
                                                               ViewCoordinate<B, Axis> target, int divisor) {
        if (divisor <= 0) throw std::domain_error("approach divisor must be positive");
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto delta = detail::signedDword(std::uint32_t(from.value_) - std::uint32_t(target.value_));
            return from - ViewDisplacement<B, Axis>(delta / divisor);
        } else return ViewCoordinate<B, Axis>(from.value_ - (from.value_ - target.value_) / divisor);
    }
    template<class Axis> static ViewCoordinate<B, Axis> interpolate(ViewCoordinate<B, Axis> from,
                                                                  ViewCoordinate<B, Axis> to, FrameFraction fraction) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto delta = detail::signedDword(std::uint32_t(to.value_) - std::uint32_t(from.value_));
            const auto magnitude = delta < 0 ? -std::int64_t(delta) : std::int64_t(delta);
            if (magnitude && fraction.numerator_ > INT64_MAX / magnitude)
                throw std::overflow_error("horizontal interpolation product overflow");
            return from + ViewDisplacement<B, Axis>(static_cast<std::int32_t>(
                std::int64_t(delta) * fraction.numerator_ / fraction.denominator_));
        } else {
            const double t = double(fraction.numerator_) / fraction.denominator_;
            return ViewCoordinate<B, Axis>(from.value_ * (1 - t) + to.value_ * t);
        }
    }
    /* Coarse map units from a fine view coordinate: 32 fine units per map
     * unit biased by +0x10 (round-to-nearest), with Y mirrored against
     * 0x8000. Modern keeps the fraction; the legacy word drops it. */
    using MapRep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    static MapRep mapUnitsX(ViewCoordinate<B, ViewXAxis> v) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return static_cast<std::int16_t>((v.value_ + 0x10) >> 5);
        else return (v.value_ + 16.0) / 32.0;
    }
    static MapRep mapUnitsY(ViewCoordinate<B, ViewYAxis> v) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return static_cast<std::int16_t>(0x8000 - ((v.value_ + 0x10) >> 5));
        else return 32768.0 - (v.value_ + 16.0) / 32.0;
    }
};
}
#endif
