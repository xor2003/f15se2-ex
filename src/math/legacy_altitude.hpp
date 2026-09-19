#ifndef F15_MATH_LEGACY_ALTITUDE_HPP
#define F15_MATH_LEGACY_ALTITUDE_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "altitude_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
namespace f15::math::legacy {
using Altitudes = AltitudeBoundary<FixedBackend>;
inline FlightAltitude<FixedBackend> altitudeFromUnits(std::uint32_t value) { return Altitudes::altitude(value); }
inline std::uint32_t altitudeUnits(FlightAltitude<FixedBackend> value) { return Altitudes::altitude(value); }
inline std::int16_t climbUnits(ClimbRate<FixedBackend> value) { return Altitudes::climb(value); }
}
#endif
