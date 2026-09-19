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
};
}
#endif
