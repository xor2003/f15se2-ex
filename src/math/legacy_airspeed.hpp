#ifndef F15_MATH_LEGACY_AIRSPEED_HPP
#define F15_MATH_LEGACY_AIRSPEED_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "airspeed_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
namespace f15::math::legacy {
using Airspeeds = AirspeedBoundary<FixedBackend>;
inline FlightSpeed<FixedBackend> speedFromUnits(std::int32_t v) { return Airspeeds::speed(v); }
inline std::int32_t speedUnits(FlightSpeed<FixedBackend> v) { return Airspeeds::speed(v); }
inline std::uint16_t speedWord(FlightSpeed<FixedBackend> v) { return std::uint16_t(speedUnits(v)); }
}
#endif
