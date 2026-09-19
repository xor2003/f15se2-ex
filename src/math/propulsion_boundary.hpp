#ifndef F15_MATH_PROPULSION_BOUNDARY_HPP
#define F15_MATH_PROPULSION_BOUNDARY_HPP
#ifndef F15_MATH_BOUNDARY_ACCESS
#error "Raw thrust conversion is restricted to reviewed boundary adapters"
#endif
#include "propulsion.hpp"
namespace f15::math {
template<class B> struct PropulsionBoundary {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    static EngineThrust<B> thrust(Rep value) { return EngineThrust<B>(value); }
    static Rep thrust(EngineThrust<B> value) { return value.value_; }
    static FuelLoad<B> fuel(typename FuelLoad<B>::Rep value) { return FuelLoad<B>(value); }
    static FlightLoad<B> load(typename FlightLoad<B>::Rep value) { return FlightLoad<B>(value); }
    static auto load(FlightLoad<B> value) { return value.value_; }
};
}
#endif
