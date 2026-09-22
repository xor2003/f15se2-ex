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

/* Fine-coordinate rep (int32 fixed / double modern) for the model-submit
 * boundary: drawWorldObject takes positions in it so the object's sub-fine
 * fraction reaches the view-frac path instead of truncating to the packed
 * int32 word — the eye fraction alone left close models stepping. */
using FineRep = f15::math::FineRep<GameBackend>;

/* SimObject.worldX/worldY shadow writes. The packed int32 field is the frozen
 * FlightUnit layout that serialization, snapshots and word-domain consumers
 * read; the shadow ViewCoordinate carries the modern sub-fine-unit fraction.
 * Every position write flows through these so the two stay in sync — under
 * fixed each reduces to the raw int32 store it replaced. */
template<class Axis, class B = GameBackend>
inline void objectFineSet(ViewCoordinate<B, Axis> &shadow, std::int32_t &packed,
                          typename HorizontalBoundary<B>::Rep v) {
    shadow = HorizontalBoundary<B>::template coordinate<Axis>(v);
    packed = static_cast<std::int32_t>(HorizontalBoundary<B>::coordinate(shadow));
}
template<class Axis, class B = GameBackend>
inline void objectFineAdvance(ViewCoordinate<B, Axis> &shadow, std::int32_t &packed,
                              typename HorizontalBoundary<B>::Rep step) {
    shadow = shadow + HorizontalBoundary<B>::template displacement<Axis>(step);
    packed = static_cast<std::int32_t>(HorizontalBoundary<B>::coordinate(shadow));
}
/* Fraction-preserving rep read for typed consumers (FineCoord seeds). */
template<class Axis, class B = GameBackend>
inline auto objectFineRep(ViewCoordinate<B, Axis> shadow) {
    return HorizontalBoundary<B>::coordinate(shadow);
}
}
#endif
