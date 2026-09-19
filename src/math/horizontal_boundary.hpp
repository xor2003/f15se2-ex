#ifndef F15_MATH_HORIZONTAL_BOUNDARY_HPP
#define F15_MATH_HORIZONTAL_BOUNDARY_HPP
#ifndef F15_MATH_BOUNDARY_ACCESS
#error "Raw horizontal conversion is restricted to reviewed boundary adapters"
#endif
#include "horizontal.hpp"
namespace f15::math {
template<class B> struct HorizontalBoundary {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int32_t, double>;
    template<class Axis> static ViewCoordinate<B, Axis> coordinate(Rep v) { return ViewCoordinate<B, Axis>(v); }
    template<class Axis> static Rep coordinate(ViewCoordinate<B, Axis> v) { return v.value_; }
    template<class Axis> static ViewDisplacement<B, Axis> displacement(Rep v) { return ViewDisplacement<B, Axis>(v); }
    template<class Axis> static Rep displacement(ViewDisplacement<B, Axis> v) { return v.value_; }
    using SpeedRep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    static HorizontalSpeed<B> speed(SpeedRep v) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(v)) throw std::domain_error("non-finite horizontal speed");
        return HorizontalSpeed<B>(v);
    }
};
}
#endif
