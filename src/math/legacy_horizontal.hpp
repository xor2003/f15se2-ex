#ifndef F15_MATH_LEGACY_HORIZONTAL_HPP
#define F15_MATH_LEGACY_HORIZONTAL_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "horizontal_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
namespace f15::math::legacy {
using Horizontal = HorizontalBoundary<GameBackend>;
inline auto viewX(std::int32_t v) { return Horizontal::coordinate<ViewXAxis>(v); }
inline auto viewY(std::int32_t v) { return Horizontal::coordinate<ViewYAxis>(v); }
inline auto moveX(std::int32_t v) { return Horizontal::displacement<ViewXAxis>(v); }
inline auto moveY(std::int32_t v) { return Horizontal::displacement<ViewYAxis>(v); }
template<class Axis> inline std::int32_t fineUnits(ViewCoordinate<GameBackend, Axis> v) { return Horizontal::coordinate(v); }
/* Untruncated fine rep — modern callers seeding fractional state read through
 * this instead of the int32 fineUnits word boundary. */
template<class Axis> inline auto fineRep(ViewCoordinate<GameBackend, Axis> v) { return Horizontal::coordinate(v); }
}
#endif
