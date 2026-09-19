#ifndef F15_MATH_LEGACY_AIRSPEED_HPP
#define F15_MATH_LEGACY_AIRSPEED_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "airspeed_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
namespace f15::math::legacy {
using Airspeeds = AirspeedBoundary<GameBackend>;
inline FlightSpeed<GameBackend> speedFromUnits(std::int32_t v) { return Airspeeds::speed(v); }
inline std::int32_t speedUnits(FlightSpeed<GameBackend> v) { return Airspeeds::speed(v); }
inline std::uint16_t speedWord(FlightSpeed<GameBackend> v) { return std::uint16_t(speedUnits(v)); }
inline std::int16_t cornerKnots(CornerSpeed<GameBackend> v) { return Airspeeds::corner(v); }
inline StallSpeed<GameBackend> stallFromUnits(std::int64_t v) {
    const auto bits = std::uint16_t(v);
    return Airspeeds::stall(static_cast<std::int16_t>(bits < 32768 ? int(bits) : int(bits) - 65536));
}
}
#endif
