#ifndef F15_MATH_AIRSPEED_BOUNDARY_HPP
#define F15_MATH_AIRSPEED_BOUNDARY_HPP
#ifndef F15_MATH_BOUNDARY_ACCESS
#error "Raw airspeed conversion is restricted to reviewed boundary adapters"
#endif
#include "airspeed.hpp"
namespace f15::math {
template<class B> struct AirspeedBoundary {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int32_t, double>;
    static FlightSpeed<B> speed(Rep v) { return FlightSpeed<B>(v); }
    static Rep speed(FlightSpeed<B> v) { return v.value_; }
    static Deceleration<B> deceleration(Rep v) { return Deceleration<B>(v); }
    using StallRep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    static StallSpeed<B> stall(StallRep v) { return StallSpeed<B>(v); }
    static StallRep stall(StallSpeed<B> v) { return v.value_; }
    static CornerSpeed<B> corner(StallRep v) { return CornerSpeed<B>(v); }
    static StallRep corner(CornerSpeed<B> v) { return v.value_; }
    // Coarse launch-speed term for the projectile table (engine velocity >> 11).
    // Modern keeps the unwrapped quotient instead of the low-word read.
    static std::int16_t projectile(FlightSpeed<B> v) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return static_cast<std::int16_t>(std::uint16_t(v.value_) >> 11);
        else
            return static_cast<std::int16_t>(std::clamp<std::int64_t>(
                static_cast<std::int64_t>(v.value_ / 2048), -32768, 32767));
    }
};
}
#endif
