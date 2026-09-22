#ifndef F15_MATH_LEGACY_PROPULSION_HPP
#define F15_MATH_LEGACY_PROPULSION_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "propulsion_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
namespace f15::math::legacy {
inline EngineThrust<GameBackend> thrustFromUnits(std::int16_t value) {
    return PropulsionBoundary<GameBackend>::thrust(value);
}
inline std::int16_t thrustUnits(EngineThrust<GameBackend> value) {
    return PropulsionBoundary<GameBackend>::thrust(value);
}
inline FuelLoad<GameBackend> fuelFromUnits(std::int16_t value) {
    return PropulsionBoundary<GameBackend>::fuel(value);
}
inline std::int16_t fuelUnits(FuelLoad<GameBackend> value) {
    return static_cast<std::int16_t>(PropulsionBoundary<GameBackend>::fuel(value));
}
inline FlightLoad<GameBackend> loadFromSixteenths(std::int32_t value) {
    return PropulsionBoundary<GameBackend>::load(value);
}
inline std::int32_t loadSixteenths(FlightLoad<GameBackend> value) {
    return PropulsionBoundary<GameBackend>::load(value);
}
}
#endif
