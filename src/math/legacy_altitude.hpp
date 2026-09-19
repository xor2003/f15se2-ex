#ifndef F15_MATH_LEGACY_ALTITUDE_HPP
#define F15_MATH_LEGACY_ALTITUDE_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "altitude_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
namespace f15::math::legacy {
using Altitudes = AltitudeBoundary<GameBackend>;
inline FlightAltitude<GameBackend> altitudeFromUnits(std::uint32_t value) { return Altitudes::altitude(value); }
inline std::uint32_t altitudeUnits(FlightAltitude<GameBackend> value) { return Altitudes::altitude(value); }
inline std::int16_t climbUnits(ClimbRate<GameBackend> value) { return Altitudes::climb(value); }
inline RenderHeight<GameBackend> renderHeightFromUnits(std::int16_t value) { return Altitudes::render(value); }
}
#endif
