#ifndef F15_MATH_LEGACY_PROPULSION_HPP
#define F15_MATH_LEGACY_PROPULSION_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "propulsion_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
namespace f15::math::legacy {
inline EngineThrust<FixedBackend> thrustFromUnits(std::int16_t value) {
    return PropulsionBoundary<FixedBackend>::thrust(value);
}
inline std::int16_t thrustUnits(EngineThrust<FixedBackend> value) {
    return PropulsionBoundary<FixedBackend>::thrust(value);
}
inline FuelLoad<FixedBackend> fuelFromUnits(std::int16_t value) {
    return PropulsionBoundary<FixedBackend>::fuel(value);
}
inline FlightLoad<FixedBackend> loadFromSixteenths(std::int32_t value) {
    return PropulsionBoundary<FixedBackend>::load(value);
}
inline std::int32_t loadSixteenths(FlightLoad<FixedBackend> value) {
    return PropulsionBoundary<FixedBackend>::load(value);
}
}
#endif
