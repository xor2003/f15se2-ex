#ifndef F15_MATH_ALTITUDE_BOUNDARY_HPP
#define F15_MATH_ALTITUDE_BOUNDARY_HPP
#ifndef F15_MATH_BOUNDARY_ACCESS
#error "Raw altitude conversion is restricted to reviewed boundary adapters"
#endif
#include "altitude.hpp"
#include <cmath>
namespace f15::math {
template<> struct AltitudeBoundary<FixedBackend> {
    static FlightAltitude<FixedBackend> altitude(std::uint32_t v) { return FlightAltitude<FixedBackend>(v); }
    static std::uint32_t altitude(FlightAltitude<FixedBackend> v) { return v.value_; }
    static ClimbRate<FixedBackend> climb(std::int16_t v) { return ClimbRate<FixedBackend>(v); }
    static std::int16_t climb(ClimbRate<FixedBackend> v) { return v.value_; }
    static TerrainHeight<FixedBackend> ground(std::int16_t v) { return TerrainHeight<FixedBackend>(v); }
    static AirspeedSample<FixedBackend> speed(std::uint16_t v) { return AirspeedSample<FixedBackend>(v); }
    static std::int16_t render(RenderHeight<FixedBackend> v) { return v.value_; }
    static RenderHeight<FixedBackend> render(std::int16_t v) { return RenderHeight<FixedBackend>(v); }
    /* The stored scene word is the value itself. */
    static std::int16_t renderWord(RenderHeight<FixedBackend> v) { return v.value_; }
};
template<> struct AltitudeBoundary<ModernBackend> {
    static FlightAltitude<ModernBackend> altitude(double v) { check(v); return FlightAltitude<ModernBackend>(v); }
    static double altitude(FlightAltitude<ModernBackend> v) { return v.value_; }
    static ClimbRate<ModernBackend> climb(double v) { check(v); return ClimbRate<ModernBackend>(v); }
    static double climb(ClimbRate<ModernBackend> v) { return v.value_; }
    static TerrainHeight<ModernBackend> ground(double v) { check(v); return TerrainHeight<ModernBackend>(v); }
    static AirspeedSample<ModernBackend> speed(double v) {
        check(v);
        if (v < 0) throw std::domain_error("airspeed must be nonnegative");
        return AirspeedSample<ModernBackend>(v);
    }
    static double render(RenderHeight<ModernBackend> v) { return v.value_; }
    static RenderHeight<ModernBackend> render(double v) { check(v); return RenderHeight<ModernBackend>(v); }
    /* Legacy 16-bit render/file words wrap modulo 2^16. Truncate through fmod
     * so the conversion is defined for any finite scene height. */
    static std::int16_t renderWord(RenderHeight<ModernBackend> v) {
        const auto wrapped = static_cast<std::int64_t>(std::fmod(v.value_, 65536.0));
        const auto bits = static_cast<std::uint16_t>(wrapped);
        return static_cast<std::int16_t>(bits < 32768 ? bits : static_cast<int>(bits) - 65536);
    }
private:
    static void check(double v) { if (!std::isfinite(v)) throw std::domain_error("non-finite vertical quantity"); }
};
}
#endif
