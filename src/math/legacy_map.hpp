#ifndef F15_MATH_LEGACY_MAP_HPP
#define F15_MATH_LEGACY_MAP_HPP
#include "map_position.hpp"
namespace f15::math {
template<class B> struct MapBoundary {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    static MapPosition<B> position(Rep x, Rep y) { return MapPosition<B>(x, y); }
};
namespace legacy {
inline MapPosition<FixedBackend> mapPosition(std::int16_t x, std::int16_t y) {
    return MapBoundary<FixedBackend>::position(x, y);
}
}
}
#endif
